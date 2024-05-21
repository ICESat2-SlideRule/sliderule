/*
 * Copyright (c) 2021, University of Washington
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the University of Washington nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY OF WASHINGTON AND CONTRIBUTORS
 * “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE UNIVERSITY OF WASHINGTON OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/******************************************************************************
 * INCLUDES
 ******************************************************************************/

#include "GeoRaster.h"
#include "GeoIndexedRaster.h"

#include <algorithm>
#include <gdal.h>
#include <gdalwarper.h>
#include <gdal_priv.h>
#include <ogr_geometry.h>
#include <ogr_spatialref.h>


/******************************************************************************
 * STATIC DATA
 ******************************************************************************/

const char* GeoIndexedRaster::FLAGS_TAG = "Fmask";
const char* GeoIndexedRaster::VALUE_TAG = "Value";

/******************************************************************************
 * PUBLIC METHODS
 ******************************************************************************/

/*----------------------------------------------------------------------------
 * Reader Constructor
 *----------------------------------------------------------------------------*/
GeoIndexedRaster::Reader::Reader (GeoIndexedRaster* raster):
    obj(raster),
    geo(NULL),
    entry(NULL),
    sync(NUM_SYNC_SIGNALS),
    run(true)
{
    thread = new Thread(GeoIndexedRaster::readingThread, this);
}

/*----------------------------------------------------------------------------
 * Reader Destructor
 *----------------------------------------------------------------------------*/
GeoIndexedRaster::Reader::~Reader (void)
{
    sync.lock();
    {
        run = false; /* Set run flag to false */
        sync.signal(DATA_TO_SAMPLE, Cond::NOTIFY_ONE);
    }
    sync.unlock();

    delete thread; /* delete thread waits on thread to join */

    /* geometry geo is cloned not 'newed' on GDAL heap. Use this call to free it */
    if(geo) OGR_G_DestroyGeometry(geo);
}

/*----------------------------------------------------------------------------
 * init
 *----------------------------------------------------------------------------*/
void GeoIndexedRaster::init (void)
{
}

/*----------------------------------------------------------------------------
 * deinit
 *----------------------------------------------------------------------------*/
void GeoIndexedRaster::deinit (void)
{
}


/*----------------------------------------------------------------------------
 * getSamples
 *----------------------------------------------------------------------------*/
uint32_t GeoIndexedRaster::getSamples(const MathLib::point_3d_t& point, int64_t gps, List<RasterSample*>& slist, void* param)
{
    static_cast<void>(param);

    samplingMutex.lock();
    try
    {
        ssError = SS_NO_ERRORS;

        OGRPoint ogr_point(point.x, point.y, point.z);

        /* Sample Rasters */
        if(sample(&ogr_point, gps))
        {
            /* Populate Return Vector of Samples (slist) */
            const GroupOrdering::Iterator iter(groupList);
            for(int i = 0; i < iter.length; i++)
            {
                const rasters_group_t* rgroup = iter[i].value;
                uint32_t flags = 0;

                /* Get flags value for this group of rasters */
                if(parms->flags_file)
                    flags = getGroupFlags(rgroup);

                getGroupSamples(rgroup, slist, flags);
            }
        }
    }
    catch (const RunTimeException &e)
    {
        mlog(e.level(), "Error getting samples: %s", e.what());
    }

    /* Free Unreturned Results */
    cacheitem_t* item;
    const char* key = cache.first(&item);
    while(key != NULL)
    {
        if(item->sample)
        {
            delete item->sample;
            item->sample = NULL;
        }
        if(item->subset)
        {
            delete item->subset;
            item->subset = NULL;
        }
        key = cache.next(&item);
    }
    samplingMutex.unlock();

    return ssError;
}

/*----------------------------------------------------------------------------
 * getSubset
 *----------------------------------------------------------------------------*/
uint32_t GeoIndexedRaster::getSubsets(const MathLib::extent_t& extent, int64_t gps, List<RasterSubset*>& slist, void* param)
{
    static_cast<void>(param);

    samplingMutex.lock();
    try
    {
        ssError = SS_NO_ERRORS;

        OGRPolygon poly = GdalRaster::makeRectangle(extent.ll.x, extent.ll.y, extent.ur.x, extent.ur.y);

        /* Sample Subsets */
        if(sample(&poly, gps))
        {
            /* Populate Return Vector of Subsets (slist) */
            const GroupOrdering::Iterator iter(groupList);
            for(int i = 0; i < iter.length; i++)
            {
                const rasters_group_t* rgroup = iter[i].value;
                getGroupSubsets(rgroup, slist);
            }
        }
    }
    catch (const RunTimeException &e)
    {
        mlog(e.level(), "Error subsetting raster: %s", e.what());
    }
    samplingMutex.unlock();

    return ssError;
}

/*----------------------------------------------------------------------------
 * Destructor
 *----------------------------------------------------------------------------*/
GeoIndexedRaster::~GeoIndexedRaster(void)
{
    emptyFeaturesList();
}

/******************************************************************************
 * PROTECTED METHODS
 ******************************************************************************/

/*----------------------------------------------------------------------------
 * Constructor
 *----------------------------------------------------------------------------*/
GeoIndexedRaster::GeoIndexedRaster(lua_State *L, GeoParms* _parms, GdalRaster::overrideCRS_t cb):
    RasterObject (L, _parms),
    cache        (MAX_READER_THREADS),
    ssError      (SS_NO_ERRORS),
    crscb        (cb),
    bbox         {0, 0, 0, 0},
    rows         (0),
    cols         (0)
{
    /* Add Lua Functions */
    LuaEngine::setAttrFunc(L, "dim", luaDimensions);
    LuaEngine::setAttrFunc(L, "bbox", luaBoundingBox);
    LuaEngine::setAttrFunc(L, "cell", luaCellSize);

    /* Establish Credentials */
    GdalRaster::initAwsAccess(_parms);

    /* Mark index file bbox/extent poly as empty */
    geoIndexPoly.empty();
}

/*----------------------------------------------------------------------------
 * getGroupSamples
 *----------------------------------------------------------------------------*/
void GeoIndexedRaster::getGroupSamples(const rasters_group_t* rgroup, List<RasterSample*>& slist, uint32_t flags)
{
    for(const auto& rinfo: rgroup->infovect)
    {
        if(strcmp(VALUE_TAG, rinfo.tag.c_str()) == 0)
        {
            const char* key = rinfo.fileName.c_str();
            cacheitem_t* item;
            if(cache.find(key, &item))
            {
                RasterSample* _sample = item->sample;
                if(_sample)
                {
                    _sample->flags = flags;
                    slist.add(_sample);
                    item->sample = NULL;
                }

                /* Get sampling/subset error status */
                ssError |= item->raster->getSSerror();
            }
        }
    }
}


/*----------------------------------------------------------------------------
 * getGroupSubsets
 *----------------------------------------------------------------------------*/
void GeoIndexedRaster::getGroupSubsets(const rasters_group_t* rgroup, List<RasterSubset*>& slist)
{
    for(const auto& rinfo: rgroup->infovect)
    {
        const char* key = rinfo.fileName.c_str();
        cacheitem_t* item;
        if(cache.find(key, &item))
        {
            RasterSubset* subset = item->subset;
            if(subset)
            {
                slist.add(subset);
                item->subset = NULL;
            }

            /* Get sampling/subset error status */
            ssError |= item->raster->getSSerror();
        }
    }
}


/*----------------------------------------------------------------------------
 * getGroupFlags
 *----------------------------------------------------------------------------*/
uint32_t GeoIndexedRaster::getGroupFlags(const rasters_group_t* rgroup)
{
    uint32_t flags = 0;

    for(const auto& rinfo: rgroup->infovect)
    {
        if(strcmp(FLAGS_TAG, rinfo.tag.c_str()) == 0)
        {
            cacheitem_t* item;
            const char* key = rinfo.fileName.c_str();
            if(cache.find(key, &item))
            {
                const RasterSample* _sample = item->sample;
                if(_sample)
                {
                    flags = _sample->value;
                }
            }
            break;
        }
    }

    return flags;
}

/*----------------------------------------------------------------------------
 * getGmtDate
 *----------------------------------------------------------------------------*/
double GeoIndexedRaster::getGmtDate(const OGRFeature* feature, const char* field,  TimeLib::gmt_time_t& gmtDate)
{
    memset(&gmtDate, 0, sizeof(TimeLib::gmt_time_t));

    const int i = feature->GetFieldIndex(field);
    if(i == -1)
    {
        mlog(ERROR, "Time field: %s not found, unable to get GMT date", field);
        return 0;
    }

    double gpstime = 0;
    double seconds;
    int year;
    int month;
    int day;
    int hour;
    int minute;

    /*
     * Raster's datetime in geojson index file should be properly formated GMT date time string in ISO8601 format.
     * Make best effort to convert it to gps time.
     */
    if(const char* iso8601date = feature->GetFieldAsISO8601DateTime(i, NULL))
    {
        if (sscanf(iso8601date, "%04d-%02d-%02dT%02d:%02d:%lfZ",
            &year, &month, &day, &hour, &minute, &seconds) == 6)
        {
            gmtDate.year = year;
            gmtDate.doy = TimeLib::dayofyear(year, month, day);
            gmtDate.hour = hour;
            gmtDate.minute = minute;
            gmtDate.second = seconds;
            gmtDate.millisecond = 0;
            gpstime = TimeLib::gmt2gpstime(gmtDate);
            // mlog(DEBUG, "%04d:%02d:%02d:%02d:%02d:%02d", year, month, day, hour, minute, (int)seconds);
        }
        else mlog(DEBUG, "Unable to parse ISO8601 UTC date string [%s]", iso8601date);
    }
    else mlog(DEBUG, "Date field is invalid");

    return gpstime;
}

/*----------------------------------------------------------------------------
 * openGeoIndex
 *----------------------------------------------------------------------------*/
bool GeoIndexedRaster::openGeoIndex(const OGRGeometry* geo)
{
    std::string newFile;
    getIndexFile(geo, newFile);

    /* Trying to open the same file? */
    if(!featuresList.empty() && newFile == indexFile)
        return true;

    GDALDataset* dset = NULL;
    try
    {
        emptyFeaturesList();
        geoIndexPoly.empty();

        /* Open new vector data set*/
        dset = static_cast<GDALDataset *>(GDALOpenEx(newFile.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, NULL, NULL, NULL));
        if (dset == NULL)
        {
            mlog(CRITICAL, "Failed to open vector index file: %s", newFile.c_str());
            throw RunTimeException(ERROR, RTE_ERROR, "Failed to open vector index file: %s:", newFile.c_str());
        }

        indexFile = newFile;
        OGRLayer* layer = dset->GetLayer(0);
        CHECKPTR(layer);

        /*
         * Clone all features and store them for performance/speed of feature lookup
         */
        layer->ResetReading();
        while(OGRFeature* feature = layer->GetNextFeature())
        {
            OGRFeature* fp = feature->Clone();
            featuresList.push_back(fp);
            OGRFeature::DestroyFeature(feature);
        }

        cols = dset->GetRasterXSize();
        rows = dset->GetRasterYSize();

        /* OGREnvelope is not treated as first classs geometry in OGR, must create a polygon geometry from it */
        OGREnvelope env;
        const OGRErr err = layer->GetExtent(&env);
        if(err == OGRERR_NONE )
        {
            bbox.lon_min = env.MinX;
            bbox.lat_min = env.MinY;
            bbox.lon_max = env.MaxX;
            bbox.lat_max = env.MaxY;

            /* Create poly geometry for index file bbox/envelope */
            geoIndexPoly = GdalRaster::makeRectangle(bbox.lon_min, bbox.lat_min, bbox.lon_max, bbox.lat_max);
            mlog(DEBUG, "index file extent/bbox: (%.6lf, %.6lf), (%.6lf, %.6lf)", bbox.lon_min, bbox.lat_min, bbox.lon_max, bbox.lat_max);
        }

        GDALClose((GDALDatasetH)dset);
        mlog(DEBUG, "Loaded %lu index file features/rasters from: %s", featuresList.size(), newFile.c_str());
    }
    catch (const RunTimeException &e)
    {
        if(dset) GDALClose((GDALDatasetH)dset);
        emptyFeaturesList();
        ssError |= SS_INDEX_FILE_ERROR;
        return false;
    }

    return true;
}


/*----------------------------------------------------------------------------
 * sampleRasters
 *----------------------------------------------------------------------------*/
void GeoIndexedRaster::sampleRasters(OGRGeometry* geo)
{
    /* Create additional reader threads if needed */
    createThreads();

    /* For each raster which is marked to be sampled, give it to the reader thread */
    int signaledReaders = 0;
    int i = 0;
    cacheitem_t* item;

    const char* key = cache.first(&item);
    while(key != NULL)
    {
        reader_t* reader = readers[i++];
        reader->sync.lock();
        {
            reader->entry = item;
            if(reader->geo) OGR_G_DestroyGeometry(reader->geo);
            reader->geo = geo->clone();
            reader->sync.signal(DATA_TO_SAMPLE, Cond::NOTIFY_ONE);
            signaledReaders++;
        }
        reader->sync.unlock();
        key = cache.next(&item);
    }

    /* Wait for readers to finish sampling */
    for(int j = 0; j < signaledReaders; j++)
    {
        reader_t* reader = readers[j];
        reader->sync.lock();
        {
            while(reader->entry != NULL)
                reader->sync.wait(DATA_SAMPLED, SYS_TIMEOUT);
        }
        reader->sync.unlock();
    }
}


/*----------------------------------------------------------------------------
 * sample
 *----------------------------------------------------------------------------*/
bool GeoIndexedRaster::sample(OGRGeometry* geo, int64_t gps)
{
    bool status = false;

    groupList.clear();

    /* For AOI always open new index file, for POI it depends... */
    const bool openNewFile = GdalRaster::ispoly(geo) || geoIndexPoly.IsEmpty() || !geoIndexPoly.Contains(geo);
    if(openNewFile)
    {
        if(!openGeoIndex(geo))
            return status;
    }

    if(findRasters(geo) && filterRasters(gps) && updateCache())
    {
        sampleRasters(geo);
        status = true;
    }

    return status;
}


/*----------------------------------------------------------------------------
 * emptyFeaturesList
 *----------------------------------------------------------------------------*/
void GeoIndexedRaster::emptyFeaturesList(void)
{
    if(featuresList.empty()) return;

    for(unsigned i = 0; i < featuresList.size(); i++)
    {
        OGRFeature* feature = featuresList[i];
        OGRFeature::DestroyFeature(feature);
    }
    featuresList.clear();
}

/******************************************************************************
 * PRIVATE METHODS
 ******************************************************************************/

/*----------------------------------------------------------------------------
 * luaDimensions - :dim() --> rows, cols
 *----------------------------------------------------------------------------*/
int GeoIndexedRaster::luaDimensions(lua_State *L)
{
    bool status = false;
    int num_ret = 1;

    try
    {
        /* Get Self */
        GeoIndexedRaster *lua_obj = dynamic_cast<GeoIndexedRaster*>(getLuaSelf(L, 1));

        /* Return dimensions of index vector file */
        lua_pushinteger(L, lua_obj->rows);
        lua_pushinteger(L, lua_obj->cols);
        num_ret += 2;

        /* Set Return Status */
        status = true;
    }
    catch(const RunTimeException& e)
    {
        mlog(e.level(), "Error getting dimensions: %s", e.what());
    }

    /* Return Status */
    return returnLuaStatus(L, status, num_ret);
}

/*----------------------------------------------------------------------------
 * luaBoundingBox - :bbox() --> (lon_min, lat_min, lon_max, lat_max)
 *----------------------------------------------------------------------------*/
int GeoIndexedRaster::luaBoundingBox(lua_State *L)
{
    bool status = false;
    int num_ret = 1;

    try
    {
        /* Get Self */
        GeoIndexedRaster *lua_obj = dynamic_cast<GeoIndexedRaster*>(getLuaSelf(L, 1));

        /* Return bbox of index vector file */
        lua_pushnumber(L, lua_obj->bbox.lon_min);
        lua_pushnumber(L, lua_obj->bbox.lat_min);
        lua_pushnumber(L, lua_obj->bbox.lon_max);
        lua_pushnumber(L, lua_obj->bbox.lat_max);
        num_ret += 4;

        /* Set Return Status */
        status = true;
    }
    catch(const RunTimeException& e)
    {
        mlog(e.level(), "Error getting bounding box: %s", e.what());
    }

    /* Return Status */
    return returnLuaStatus(L, status, num_ret);
}

/*----------------------------------------------------------------------------
 * luaCellSize - :cell() --> cell size
 *----------------------------------------------------------------------------*/
int GeoIndexedRaster::luaCellSize(lua_State *L)
{
    bool status = false;
    int num_ret = 1;

    try
    {
        /* Cannot return cell sizes of index vector file */
        const int cellSize = 0;

        /* Set Return Values */
        lua_pushnumber(L, cellSize);
        num_ret += 1;

        /* Set Return Status */
        status = true;
    }
    catch(const RunTimeException& e)
    {
        mlog(e.level(), "Error getting cell size: %s", e.what());
    }

    /* Return Status */
    return returnLuaStatus(L, status, num_ret);
}

/*----------------------------------------------------------------------------
 * readingThread
 *----------------------------------------------------------------------------*/
void* GeoIndexedRaster::readingThread(void *param)
{
    reader_t *reader = static_cast<reader_t*>(param);

    while(reader->run)
    {
        reader->sync.lock();
        {
            /* Wait for raster to work on */
            while((reader->entry == NULL) && reader->run)
                reader->sync.wait(DATA_TO_SAMPLE, SYS_TIMEOUT);
        }
        reader->sync.unlock();

        cacheitem_t* entry = reader->entry;
        if(entry != NULL)
        {
            if(GdalRaster::ispoint(reader->geo))
            {
                entry->sample = entry->raster->samplePOI(dynamic_cast<OGRPoint*>(reader->geo));
            }
            else if(GdalRaster::ispoly(reader->geo))
            {
                entry->subset = entry->raster->subsetAOI(dynamic_cast<OGRPolygon*>(reader->geo));
                if(entry->subset)
                {
                    /*
                     * Create new GeoRaster object for subsetted raster
                     * Use NULL for LuaState, using parent's causes memory corruption
                     * NOTE: cannot use RasterObject::cppCreate(parms) here, it would create
                     * new GeoIndexRaster with the same file path as parent raster.
                     */
                    entry->subset->robj = new GeoRaster(NULL,
                                                        reader->obj->parms,
                                                        entry->subset->rasterName,
                                                        entry->raster->getGpsTime(),
                                                        entry->raster->isElevation(),
                                                        entry->raster->getOverrideCRS());

                    /* GeoParms are shared with subsseted raster and other readers */
                    GeoIndexedRaster::referenceLuaObject(reader->obj->parms);
                }
            }
            entry->enabled = false; /* raster samples/subsetted */

            reader->sync.lock();
            {
                reader->entry = NULL; /* Done with this raster */
                reader->sync.signal(DATA_SAMPLED, Cond::NOTIFY_ONE);
            }
            reader->sync.unlock();
        }
    }

    return NULL;
}

/*----------------------------------------------------------------------------
 * createThreads
 *----------------------------------------------------------------------------*/
void GeoIndexedRaster::createThreads(void)
{
    const int threadsNeeded = cache.length();
    const int threadsNow    = readers.length();
    const int newThreadsCnt = threadsNeeded - threadsNow;

    if(threadsNeeded <= threadsNow)
        return;

    for(int i = 0; i < newThreadsCnt; i++)
    {
        Reader* r = new Reader(this);
        readers.add(r);
    }
    assert(readers.length() == threadsNeeded);
}

/*----------------------------------------------------------------------------
 * updateCache
 *----------------------------------------------------------------------------*/
bool GeoIndexedRaster::updateCache(void)
{
    /* Cache contains items/rasters from previous sample run */
    const GroupOrdering::Iterator group_iter(groupList);
    for(int i = 0; i < group_iter.length; i++)
    {
        const rasters_group_t* rgroup = group_iter[i].value;
        for(const auto& rinfo : rgroup->infovect)
        {
            const char* key = rinfo.fileName.c_str();
            cacheitem_t* item;
            const bool inCache = cache.find(key, &item);
            if(!inCache)
            {
                /* Limit area of interest to the extent of vector index file */
                parms->aoi_bbox = bbox;

                /* Create new cache item with raster */
                item = new cacheitem_t;
                item->raster = new GdalRaster(parms, rinfo.fileName,
                                              static_cast<double>(rgroup->gpsTime / 1000),
                                              fileDictAdd(rinfo.fileName),
                                              rinfo.dataIsElevation, crscb);
                item->sample = NULL;
                item->subset = NULL;
                const bool status = cache.add(key, item);
                assert(status); (void)status; // cannot fail; prevents linter warnings
            }

            /* Mark as Enabled */
            item->enabled = true;
        }
    }

    /*
     * Maintain cache from getting too big.
     * Find all cache items not needed for this sample run.
     */
    std::vector<const char*> keys_to_remove;
    {
        cacheitem_t* item;
        const char* key = cache.first(&item);
        while(key != NULL)
        {
            if(!item->enabled)
            {
                keys_to_remove.push_back(key);
            }
            key = cache.next(&item);
        }
    }

    /* Remove cache items found above */
    for(const char* key: keys_to_remove)
    {
        cache.remove(key);
    }

    /* Check for max limit of concurent reading raster threads */
    if(cache.length() > MAX_READER_THREADS)
    {
        ssError |= SS_THREADS_LIMIT_ERROR;
        mlog(ERROR, "Too many rasters to read: %d, max allowed: %d", cache.length(), MAX_READER_THREADS);
        return false;
    }

    return true;
}

/*----------------------------------------------------------------------------
 * filterRasters
 *----------------------------------------------------------------------------*/
bool GeoIndexedRaster::filterRasters(int64_t gps)
{
    /* URL and temporal filter - remove the whole raster group if one of rasters needs to be filtered out */
    if(parms->url_substring || parms->filter_time || parms->filter_doy_range)
    {
        const GroupOrdering::Iterator group_iter(groupList);
        for(int i = 0; i < group_iter.length; i++)
        {
            const rasters_group_t* rgroup = group_iter[i].value;
            bool removeGroup = false;

            for(const auto& rinfo : rgroup->infovect)
            {
                /* URL filter */
                if(parms->url_substring)
                {
                    if(rinfo.fileName.find(parms->url_substring) == std::string::npos)
                    {
                        removeGroup = true;
                        break;
                    }
                }

                /* Temporal filter */
                if(parms->filter_time)
                {
                    if(!TimeLib::gmtinrange(rgroup->gmtDate, parms->start_time, parms->stop_time))
                    {
                        removeGroup = true;
                        break;
                    }
                }

                /* Day Of Year filter */
                if(parms->filter_doy_range)
                {
                    const bool inrange = TimeLib::doyinrange(rgroup->gmtDate, parms->doy_start, parms->doy_end);
                    if(parms->doy_keep_inrange)
                    {
                        if(!inrange)
                        {
                            removeGroup = true;
                            break;
                        }
                    }
                    else /* Filter out rasters in doy range */
                    {
                        if(inrange)
                        {
                            removeGroup = true;
                            break;
                        }
                    }
                }
            }

            if(removeGroup)
            {
                groupList.remove(group_iter[i].key);
            }
        }
    }

    /* Closest time filter - using raster group time, not individual reaster time */
    int64_t closestGps = 0;
    if(gps > 0)
    {
        /* Caller provided gps time, use it insead of time from params */
        closestGps = gps;
    }
    else if (parms->filter_closest_time)
    {
        /* Params provided closest time */
        closestGps = TimeLib::gmt2gpstime(parms->closest_time);
    }

    if(closestGps > 0)
    {
        int64_t minDelta = abs(std::numeric_limits<int64_t>::max() - closestGps);

        /* Find raster group with the closest time */
        const GroupOrdering::Iterator group_iter(groupList);
        for(int i = 0; i < group_iter.length; i++)
        {
            const rasters_group_t* rgroup = group_iter[i].value;
            const int64_t gpsTime = rgroup->gpsTime;
            const int64_t delta   = abs(closestGps - gpsTime);

            if(delta < minDelta)
                minDelta = delta;
        }

        /* Remove all groups with greater time delta */
        for(int i = 0; i < group_iter.length; i++)
        {
            const rasters_group_t* rgroup = group_iter[i].value;
            const int64_t gpsTime = rgroup->gpsTime;
            const int64_t delta   = abs(closestGps - gpsTime);

            if(delta > minDelta)
            {
                groupList.remove(group_iter[i].key);
            }
        }
    }

    return (!groupList.empty());
}
