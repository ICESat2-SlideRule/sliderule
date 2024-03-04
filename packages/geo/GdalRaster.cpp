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

#include "RasterSample.h"
#include "GdalRaster.h"

#ifdef __aws__
#include "aws.h"
#endif

#include <algorithm>
#include <uuid/uuid.h>


/******************************************************************************
 * STATIC DATA
 ******************************************************************************/

/******************************************************************************
 * PUBLIC METHODS
 ******************************************************************************/

/*----------------------------------------------------------------------------
 * Constructor
 *----------------------------------------------------------------------------*/
GdalRaster::GdalRaster(GeoParms* _parms, const std::string& _fileName, double _gpsTime, uint64_t _fileId, bool _dataIsElevation, overrideCRS_t cb) :
   parms      (_parms),
   gpsTime    (_gpsTime),
   fileId     (_fileId),
   transf     (NULL),
   overrideCRS(cb),
   fileName   (_fileName),
   dset       (NULL),
   band       (NULL),
   dataIsElevation(_dataIsElevation),
   xsize      (0),
   ysize      (0),
   cellSize   (0),
   bbox       (),
   radiusInPixels(0),
   ssError    (SS_NO_ERRORS)
{
}

/*----------------------------------------------------------------------------
 * Destructor
 *----------------------------------------------------------------------------*/
GdalRaster::~GdalRaster(void)
{
    if(dset) GDALClose((GDALDatasetH)dset);
    if(transf) OGRCoordinateTransformation::DestroyCT(transf);
}

/*----------------------------------------------------------------------------
 * open
 *----------------------------------------------------------------------------*/
void GdalRaster::open(void)
{
    if(dset)
    {
        mlog(DEBUG, "Raster already opened: %s", fileName.c_str());
        return;
    }

    dset = (GDALDataset*)GDALOpenEx(fileName.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY, NULL, NULL, NULL);
    if(dset == NULL)
        throw RunTimeException(CRITICAL, RTE_ERROR, "Failed to open raster: %s:", fileName.c_str());

    mlog(DEBUG, "Opened %s", fileName.c_str());

    /* Store information about raster */
    xsize = dset->GetRasterXSize();
    ysize = dset->GetRasterYSize();

    CPLErr err = dset->GetGeoTransform(geoTransform);
    CHECK_GDALERR(err);
    if(!GDALInvGeoTransform(geoTransform, invGeoTransform))
    {
        throw RunTimeException(CRITICAL, RTE_ERROR, "Failed to get inverted geo transform: %s:", fileName.c_str());
    }

    /* Get raster boundry box */
    CHECK_GDALERR(err);
    bbox.lon_min = geoTransform[0];
    bbox.lon_max = geoTransform[0] + xsize * geoTransform[1];
    bbox.lat_max = geoTransform[3];
    bbox.lat_min = geoTransform[3] + ysize * geoTransform[5];

    // mlog(DEBUG, "Extent: (%.2lf, %.2lf), (%.2lf, %.2lf)", bbox.lon_min, bbox.lat_min, bbox.lon_max, bbox.lat_max);

    cellSize       = geoTransform[1];
    radiusInPixels = radius2pixels(parms->sampling_radius);

    /* Limit maximum sampling radius */
    if(radiusInPixels > MAX_SAMPLING_RADIUS_IN_PIXELS)
    {
        throw RunTimeException(CRITICAL, RTE_ERROR,
                               "Sampling radius is too big: %d: max allowed %d meters",
                               parms->sampling_radius, MAX_SAMPLING_RADIUS_IN_PIXELS * static_cast<int>(cellSize));
    }

    band = dset->GetRasterBand(1);
    CHECKPTR(band);

    /* Create coordinates transform for raster */
    createTransform();
}

/*----------------------------------------------------------------------------
 * samplePOI
 *----------------------------------------------------------------------------*/
RasterSample* GdalRaster::samplePOI(OGRPoint* poi, bool dryrun)
{
    RasterSample* sample = NULL;

    /* Clear sample/subset error status */
    ssError = SS_NO_ERRORS;

    if(dryrun)
    {
        sample = new RasterSample(gpsTime, fileId, 0);
        return sample;
    }

    try
    {
        if(dset == NULL)
            open();

        double z = poi->getZ();
        //mlog(DEBUG, "Before transform x,y,z: (%.4lf, %.4lf, %.4lf)", poi->getX(), poi->getY(), poi->getZ());
        if(poi->transform(transf) != OGRERR_NONE)
            throw RunTimeException(CRITICAL, RTE_ERROR, "Coordinates Transform failed for x,y,z (%lf, %lf, %lf)", poi->getX(), poi->getY(), poi->getZ());
        //mlog(DEBUG, "After  transform x,y,z: (%.4lf, %.4lf, %.4lf)", poi->getX(), poi->getY(), poi->getZ());

        /*
         * Attempt to read raster only if it contains the point of interest.
         */
        if((poi->getX() >= bbox.lon_min) && (poi->getX() <= bbox.lon_max) &&
           (poi->getY() >= bbox.lat_min) && (poi->getY() <= bbox.lat_max))
        {
            double vertical_shift = z - poi->getZ();
            sample = new RasterSample(gpsTime, fileId, vertical_shift);
            if(parms->sampling_algo == GRIORA_NearestNeighbour)
                readPixel(poi, sample);
            else
                resamplePixel(poi, sample);

            if(parms->zonal_stats)
                computeZonalStats(poi, sample);
        }
        else
        {
            ssError |= SS_POI_OUT_OF_BOUNDS_ERROR;
        }
    }
    catch (const RunTimeException &e)
    {
        if(sample)
        {
            delete sample;
            sample = NULL;
        }
        mlog(e.level(), "Error sampling: %s", e.what());
    }

    return sample;
}


/*----------------------------------------------------------------------------
 * subsetAOI
 *----------------------------------------------------------------------------*/
RasterSubset* GdalRaster::subsetAOI(OGRPolygon* poly)
{
    /*
     * Notes on extent format:
     * gdalwarp uses '-te xmin ymin xmax ymax'
     * gdalbuildvrt uses '-te xmin ymin xmax ymax'
     * gdal_translate uses '-projwin ulx uly lrx lry' or '-projwin xmin ymax xmax ymin'
     *
     * This function uses 'xmin ymin xmax ymax' for geo and map extent
     *                    'ulx uly lrx lry' for pixel extent
     */

    /* For debug tracing serialize all subset threads or debug messages will make no sense */
#define SUBSET_DEBUG_TRACE 0

#if SUBSET_DEBUG_TRACE
    static Mutex mutex;
    mutex.lock();
    mlog(CRITICAL, "Runing with serialized subsetAOI (very slow!!!)");
#endif

    RasterSubset* subset = NULL;

    /* Clear sample/subset error status */
    ssError = SS_NO_ERRORS;

    try
    {
        if(dset == NULL)
            open();

        OGREnvelope env;
        poly->getEnvelope(&env);
        if(SUBSET_DEBUG_TRACE) mlog(DEBUG, "geo aoi:     (%13.04lf, %13.04lf) (%13.04lf, %13.04lf)", env.MinX, env.MinY, env.MaxX, env.MaxY);

        /* Project AOI to map/raster coordinates */
        if(!transf->Transform(1, &env.MinX, &env.MinY))
            throw RunTimeException(CRITICAL, RTE_ERROR, "Coordinates Transform failed for (%.2lf, %.2lf)", env.MinX, env.MinY);
        if(!transf->Transform(1, &env.MaxX, &env.MaxY))
            throw RunTimeException(CRITICAL, RTE_ERROR, "Coordinates Transform failed for (%.2lf, %.2lf)", env.MaxX, env.MaxY);

        double aoi_minx = std::min(env.MinX, env.MaxX);
        double aoi_maxx = std::max(env.MinX, env.MaxX);
        double aoi_miny = std::min(env.MinY, env.MaxY);
        double aoi_maxy = std::max(env.MinY, env.MaxY);
        if(SUBSET_DEBUG_TRACE) mlog(DEBUG, "map aoi:     (%13.04lf, %13.04lf) (%13.04lf, %13.04lf)", aoi_minx, aoi_miny, aoi_maxx, aoi_maxy);

        double raster_minx = bbox.lon_min;
        double raster_miny = bbox.lat_min;
        double raster_maxx = bbox.lon_max;
        double raster_maxy = bbox.lat_max;
        if(SUBSET_DEBUG_TRACE) mlog(DEBUG, "map raster:  (%13.04lf, %13.04lf) (%13.04lf, %13.04lf)", raster_minx, raster_miny, raster_maxx, raster_maxy);

        /*
         * Check for AOI to be outside of raster bounds (no intersect at all)
         * It is possible that after projecting into map coordinates the AOI is no longer intersecting the raster.
         * This is not an error.
         */
        if(aoi_maxx < raster_minx)
            throw RunTimeException(DEBUG, RTE_INFO, "AOI out of bounds, aoi_max < raster_minx");

        if(aoi_minx > raster_maxx)
            throw RunTimeException(DEBUG, RTE_INFO, "AOI out of bounds, aoi_minx > raster_maxx");

        if(aoi_maxy < raster_miny)
            throw RunTimeException(DEBUG, RTE_INFO, "AOI out of bounds, aoi_maxy < raster_miny");

        if(aoi_miny > raster_maxy)
            throw RunTimeException(DEBUG, RTE_INFO, "AOI out of bounds, aoi_miny  > raster_maxy");

        /* Adjust AOI to the raster */
        if(aoi_minx < raster_minx)
        {
            if(SUBSET_DEBUG_TRACE) mlog(DEBUG, "Clipped aoi_minx %.04lf to raster_minx %.04lf", aoi_minx, raster_minx);
            aoi_minx = raster_minx;
        }
        if(aoi_miny < raster_miny)
        {
            if(SUBSET_DEBUG_TRACE) mlog(DEBUG, "Clipped aoi_miny %.04lf to raster_miny %.04lf", aoi_miny, raster_miny);
            aoi_miny = raster_miny;
        }
        if(aoi_maxx > raster_maxx)
        {
            if(SUBSET_DEBUG_TRACE) mlog(DEBUG, "Clipped aoi_maxx %.04lf to raster_maxx %.04lf", aoi_maxx, raster_maxx);
            aoi_maxx = raster_maxx;
        }
        if(aoi_maxy > raster_maxy)
        {
            if(SUBSET_DEBUG_TRACE) mlog(DEBUG, "Clipped aoi_maxy %.04lf to raster_maxy %.04lf", aoi_maxy, raster_maxy);
            aoi_maxy = raster_maxy;
        }

        if(SUBSET_DEBUG_TRACE) mlog(DEBUG, "map aoi:     (%13.04lf, %13.04lf) (%13.04lf, %13.04lf)", aoi_minx, aoi_miny, aoi_maxx, aoi_maxy);

        /* Get AOI pixel corners: upper left, lower right */
        int ulx;
        int uly;
        int lrx;
        int lry;
        map2pixel(aoi_minx, aoi_maxy, ulx, uly);
        map2pixel(aoi_maxx, aoi_miny, lrx, lry);
        if(SUBSET_DEBUG_TRACE) mlog(DEBUG, "pixel aoi:   (%13d, %13d) (%13d, %13d)", ulx, uly, lrx, lry);

        uint32_t _xsize = lrx - ulx;
        uint32_t _ysize = lry - uly;

        /* Sanity check for GCC optimizer 'bug'. Raster's top left corner pixel must be (0, 0) */
        int raster_ulx;
        int raster_uly;
        map2pixel(raster_minx, raster_maxy, raster_ulx, raster_uly);
        if(raster_ulx != 0 || raster_uly != 0)
        {
            ssError |= SS_AOI_OUT_OF_BOUNDS_ERROR;
            throw RunTimeException(CRITICAL, RTE_ERROR, "Raster's upleft pixel (%d, %d) is not (0, 0)", raster_ulx, raster_uly);
        }

        /* Sanity check for AOI top left corner pixel, must be < raster */
        if(ulx < raster_ulx || uly < raster_uly)
        {
            ssError |= SS_AOI_OUT_OF_BOUNDS_ERROR;
            throw RunTimeException(CRITICAL, RTE_ERROR, "AOI upleft pixel (%d, %d) < raster upleft pixel (%d, %d)", ulx, uly, raster_ulx, raster_uly);
        }

        subset = getRasterSubset(ulx, uly, aoi_minx, aoi_maxy, _xsize, _ysize);
    }
    catch (const RunTimeException &e)
    {
        mlog(e.level(), "Error subsetting: %s", e.what());
    }

#if SUBSET_DEBUG_TRACE
    mutex.unlock();
#endif

    return subset;
}


/*----------------------------------------------------------------------------
 * subsetAOI
 *----------------------------------------------------------------------------*/
RasterSubset* GdalRaster::subsetAOI(uint32_t ulx, uint32_t uly, uint32_t _xsize, uint32_t _ysize)
{
    RasterSubset* subset = NULL;

    /* Clear sample/subset error status */
    ssError = SS_NO_ERRORS;

    try
    {
        if(dset == NULL)
            open();

        if(ulx >= xsize)
            throw RunTimeException(CRITICAL, RTE_ERROR, "Upleft pixel's x out of bounds: %u", ulx);

        if(uly >= ysize)
            throw RunTimeException(CRITICAL, RTE_ERROR, "Upleft pixel's y out of bounds: %u", uly);

        if(_xsize == 0)
        {
            /* Read all raster columns starting at ulx */
            _xsize = xsize - ulx;
        }

        if(_ysize == 0)
        {
            /* Read all raster rows starting at uly */
            _ysize = ysize - uly;
        }

        if(ulx + _xsize > xsize)
            throw RunTimeException(CRITICAL, RTE_ERROR, "columns out of bounds");

        if(uly + _ysize > ysize)
            throw RunTimeException(CRITICAL, RTE_ERROR, "rows out of bounds");

        double map_ulx, map_uly;
        pixel2map(ulx, uly, map_ulx, map_uly);
        // mlog(DEBUG, "upleft pixel:   (%13d, %13d) (%.6lf, %.6lf)", ulx, uly, map_ulx, map_uly);

        subset = getRasterSubset(ulx, uly, map_ulx, map_uly, _xsize, _ysize);
    }
    catch (const RunTimeException &e)
    {
        mlog(e.level(), "Error subsetting: %s", e.what());
    }

    return subset;
}


/*----------------------------------------------------------------------------
 * setCRSfromWkt
 *----------------------------------------------------------------------------*/
void GdalRaster::setCRSfromWkt(OGRSpatialReference& sref, const char* wkt)
{
    // mlog(DEBUG, "%s", wkt.c_str());
    OGRErr ogrerr = sref.importFromWkt(wkt);
    CHECK_GDALERR(ogrerr);
}


/*----------------------------------------------------------------------------
 * getUUID
 *----------------------------------------------------------------------------*/
std::string GdalRaster::getUUID(void)
{
    char uuid_str[UUID_STR_LEN];
    uuid_t uuid;
    uuid_generate(uuid);
    uuid_unparse_lower(uuid, uuid_str);
    return std::string(uuid_str);
}


/*----------------------------------------------------------------------------
 * initAwsAccess
 *----------------------------------------------------------------------------*/
void GdalRaster::initAwsAccess(GeoParms* _parms)
{
    if(_parms->asset)
    {
#ifdef __aws__
        const char* path = _parms->asset->getPath();
        const char* identity = _parms->asset->getIdentity();
        CredentialStore::Credential credentials = CredentialStore::get(identity);
        if(credentials.provided)
        {
            VSISetPathSpecificOption(path, "AWS_ACCESS_KEY_ID", credentials.accessKeyId);
            VSISetPathSpecificOption(path, "AWS_SECRET_ACCESS_KEY", credentials.secretAccessKey);
            VSISetPathSpecificOption(path, "AWS_SESSION_TOKEN", credentials.sessionToken);
        }
        else
        {
            /* same as AWS CLI option '--no-sign-request' */
            VSISetPathSpecificOption(path, "AWS_NO_SIGN_REQUEST", "YES");
        }
#endif
    }
}


/*----------------------------------------------------------------------------
 * makeRectangle
 *----------------------------------------------------------------------------*/
OGRPolygon GdalRaster::makeRectangle(double minx, double miny, double maxx, double maxy)
{
    OGRPolygon poly;
    OGRLinearRing lr;

    /* Clockwise for interior of polygon */
    lr.addPoint(minx, miny);
    lr.addPoint(minx, maxy);
    lr.addPoint(maxx, maxy);
    lr.addPoint(maxx, miny);
    lr.addPoint(minx, miny);
    poly.addRing(&lr);
    return poly;
}



/******************************************************************************
 * PROTECTED METHODS
 ******************************************************************************/

/******************************************************************************
 * PRIVATE METHODS
 ******************************************************************************/

/*----------------------------------------------------------------------------
 * readPixel
 *----------------------------------------------------------------------------*/
void GdalRaster::readPixel(const OGRPoint* poi, RasterSample* sample)
{
    /* Use fast method recomended by GDAL docs to read individual pixel */
    try
    {
        int x;
        int y;
        map2pixel(poi, x, y);

        // mlog(DEBUG, "%dP, %dL\n", x, y);

        int xBlockSize = 0;
        int yBlockSize = 0;
        band->GetBlockSize(&xBlockSize, &yBlockSize);

        /* Raster offsets to block of interest */
        int xblk = x / xBlockSize;
        int yblk = y / yBlockSize;

        GDALRasterBlock* block = NULL;
        int cnt = 1;
        do
        {
            /* Retry read if error */
            block = band->GetLockedBlockRef(xblk, yblk, false);
        } while(block == NULL && cnt-- && s3sleep());
        CHECKPTR(block);

        /* Get data block pointer, no memory copied but block is locked */
        void* data = block->GetDataRef();
        if (data == NULL)
        {
            /* Before bailing release the block... */
            block->DropLock();
            CHECKPTR(data);
        }

        /* Calculate x, y inside of block */
        int _x = x % xBlockSize;
        int _y = y % yBlockSize;
        int offset = _y * xBlockSize + _x;

        /* Be carefull using offset based on the pixel data type */
        switch(band->GetRasterDataType())
        {
            case GDT_Byte:
            {
                uint8_t* p   = static_cast<uint8_t*>(data);
                sample->value = p[offset];
            }
            break;

            case GDT_UInt16:
            {
                uint16_t* p  = static_cast<uint16_t*>(data);
                sample->value = p[offset];
            }
            break;

            case GDT_Int16:
            {
                int16_t* p   = static_cast<int16_t*>(data);
                sample->value = p[offset];
            }
            break;

            case GDT_UInt32:
            {
                uint32_t* p  = static_cast<uint32_t*>(data);
                sample->value = p[offset];
            }
            break;

            case GDT_Int32:
            {
                int32_t* p   = static_cast<int32_t*>(data);
                sample->value = p[offset];
            }
            break;

            case GDT_Int64:
            {
                int64_t* p   = static_cast<int64_t*>(data);
                sample->value = p[offset];
            }
            break;

            case GDT_UInt64:
            {
                uint64_t* p  = static_cast<uint64_t*>(data);
                sample->value = p[offset];
            }
            break;

            case GDT_Float32:
            {
                float* p     = static_cast<float*>(data);
                sample->value = p[offset];
            }
            break;

            case GDT_Float64:
            {
                double* p    = static_cast<double*>(data);
                sample->value = p[offset];
            }
            break;

            default:
                /*
                 * Complex numbers are supported but not needed at this point.
                 */
                block->DropLock();
                throw RunTimeException(CRITICAL, RTE_ERROR, "Unsuported data type in raster: %s:", fileName.c_str());
        }

        /* Done reading, release block lock */
        block->DropLock();
        if(nodataCheck(sample) && dataIsElevation)
        {
            sample->value += sample->verticalShift;
        }

        // mlog(DEBUG, "Value: %.2lf, x: %u, y: %u, xblk: %u, yblk: %u, bcol: %u, brow: %u, offset: %u",
        //      sample->value, x, y, xblk, yblk, _x, _y, offset);
    }
    catch (const RunTimeException &e)
    {
        ssError |= SS_POI_FAILED_TO_READ_ERROR;
        mlog(e.level(), "Error reading from raster: %s", e.what());
        throw;
    }
}

/*----------------------------------------------------------------------------
 * resamplePixel
 *----------------------------------------------------------------------------*/
void GdalRaster::resamplePixel(const OGRPoint* poi, RasterSample* sample)
{
    try
    {
        int windowSize;
        int offset;
        int x;
        int y;
        map2pixel(poi, x, y);

        /* If zero radius provided, use defaul kernels for each sampling algorithm */
        if(parms->sampling_radius == 0)
        {
            int kernel = 0;

            if (parms->sampling_algo == GRIORA_Bilinear)
                kernel = 2; /* 2x2 kernel */
            else if (parms->sampling_algo == GRIORA_Cubic)
                kernel = 4; /* 4x4 kernel */
            else if (parms->sampling_algo == GRIORA_CubicSpline)
                kernel = 4; /* 4x4 kernel */
            else if (parms->sampling_algo == GRIORA_Lanczos)
                kernel = 6; /* 6x6 kernel */
            else if (parms->sampling_algo == GRIORA_Average)
                kernel = 6; /* No default kernel, pick something */
            else if (parms->sampling_algo == GRIORA_Mode)
                kernel = 6; /* No default kernel, pick something */
            else if (parms->sampling_algo == GRIORA_Gauss)
                kernel = 6; /* No default kernel, pick something */

            windowSize = kernel + 1;    // Odd window size around pixel
            offset = (kernel / 2);
        }
        else
        {
            windowSize = radiusInPixels * 2 + 1;  // Odd window size around pixel
            offset = radiusInPixels;
        }

        int _x = x - offset;
        int _y = y - offset;

        GDALRasterIOExtraArg args;
        INIT_RASTERIO_EXTRA_ARG(args);
        args.eResampleAlg = parms->sampling_algo;

        bool validWindow = containsWindow(_x, _y, xsize, ysize, windowSize);
        if(validWindow)
        {
            readRasterWithRetry(_x, _y, windowSize, windowSize, &sample->value, 1, 1, &args);
            if(nodataCheck(sample) && dataIsElevation)
            {
                sample->value += sample->verticalShift;
            }
        }
        else
        {
            /* At least return pixel value if unable to resample raster */
            readPixel(poi, sample);
        }
    }
    catch (const RunTimeException &e)
    {
        mlog(e.level(), "Error resampling pixel: %s", e.what());
        throw;
    }
}

/*----------------------------------------------------------------------------
 * computeZonalStats
 *----------------------------------------------------------------------------*/
void GdalRaster::computeZonalStats(const OGRPoint* poi, RasterSample* sample)
{
    double *samplesArray = NULL;

    try
    {
        int x;
        int y;
        map2pixel(poi, x, y);

        int windowSize = radiusInPixels * 2 + 1; // Odd window size around pixel
        int newx = x - radiusInPixels;
        int newy = y - radiusInPixels;

        GDALRasterIOExtraArg args;
        INIT_RASTERIO_EXTRA_ARG(args);
        args.eResampleAlg = parms->sampling_algo;
        samplesArray = new double[windowSize*windowSize];

        bool validWindow = containsWindow(newx, newy, xsize, ysize, windowSize);
        if(validWindow)
        {
            readRasterWithRetry(newx, newy, windowSize, windowSize, samplesArray, windowSize, windowSize, &args);

            /* One of the windows (raster or index data set) was valid. Compute zonal stats */
            double min = std::numeric_limits<double>::max();
            double max = std::numeric_limits<double>::min();
            double sum = 0;
            double nodata = band->GetNoDataValue();
            std::vector<double> validSamples;
            /*
             * Only use pixels within radius from pixel containing point of interest.
             * Ignore nodata values.
             */
            const double x1 = x;
            const double y1 = y;

            for(int _y = 0; _y < windowSize; _y++)
            {
                for(int _x = 0; _x < windowSize; _x++)
                {
                    double value = samplesArray[_y*windowSize + _x];
                    if(value == nodata) continue;

                    if(dataIsElevation)
                        value += sample->verticalShift;

                    double x2 = _x + newx;  /* Current pixel in buffer */
                    double y2 = _y + newy;
                    double xd = std::pow(x2 - x1, 2);
                    double yd = std::pow(y2 - y1, 2);
                    double d  = std::sqrt(xd + yd);

                    if(std::islessequal(d, radiusInPixels))
                    {
                        if(value < min) min = value;
                        if(value > max) max = value;
                        sum += value;
                        validSamples.push_back(value);
                    }
                }
            }

            int validSamplesCnt = validSamples.size();
            if(validSamplesCnt > 0)
            {
                double stdev = 0;  /* Standard deviation */
                double mad   = 0;  /* Median absolute deviation (MAD) */
                double mean  = sum / validSamplesCnt;

                for(int i = 0; i < validSamplesCnt; i++)
                {
                    double value = validSamples[i];
                    stdev += std::pow(value - mean, 2);
                    mad   += std::fabs(value - mean);
                }

                stdev = std::sqrt(stdev / validSamplesCnt);
                mad   = mad / validSamplesCnt;

                /*
                 * Calculate median
                 * For performance use nth_element algorithm from std library since it sorts only part of the vector
                 * NOTE: (vector will be reordered by nth_element)
                 */
                std::size_t n = validSamplesCnt / 2;
                std::nth_element(validSamples.begin(), validSamples.begin() + n, validSamples.end());
                double median = validSamples[n];
                if(!(validSamplesCnt & 0x1))
                {
                    /* Even number of samples, calculate average of two middle samples */
                    std::nth_element(validSamples.begin(), validSamples.begin() + n-1, validSamples.end());
                    median = (median + validSamples[n-1]) / 2;
                }

                /* Store calculated zonal stats */
                sample->stats.count  = validSamplesCnt;
                sample->stats.min    = min;
                sample->stats.max    = max;
                sample->stats.mean   = mean;
                sample->stats.median = median;
                sample->stats.stdev  = stdev;
                sample->stats.mad    = mad;
            }
        }
        else
        {
            throw RunTimeException(WARNING, RTE_ERROR, "sampling window outside of raster bbox");
        }
    }
    catch(const RunTimeException& e)
    {
        mlog(e.level(), "Error computing zonal stats: %s", e.what());
        delete[] samplesArray;
        throw;
    }

    delete[] samplesArray;
}

/*----------------------------------------------------------------------------
 * nodataCheck
 *----------------------------------------------------------------------------*/
bool GdalRaster::nodataCheck(RasterSample* sample)
{
    /*
     * Replace nodata with NAN
     */
    const double a = band->GetNoDataValue();
    const double b = sample->value;
    const double epsilon = 0.000001;

    if(std::fabs(a-b) < epsilon)
    {
        sample->value = std::nanf("");
        return false;
    }

    return true;
}


/*----------------------------------------------------------------------------
 * createTransform
 *----------------------------------------------------------------------------*/
void GdalRaster::createTransform(void)
{
    OGRErr ogrerr = sourceCRS.importFromEPSG(SLIDERULE_EPSG);
    CHECK_GDALERR(ogrerr);

    const char* projref = dset->GetProjectionRef();
    CHECKPTR(projref);
    ogrerr = targetCRS.importFromWkt(projref);
    CHECK_GDALERR(ogrerr);
    // mlog(DEBUG, "CRS from raster: %s", projref);

    if(overrideCRS)
    {
        ogrerr = overrideCRS(targetCRS);
        CHECK_GDALERR(ogrerr);
        // mlog(DEBUG, "CRS from raster: %s", projref);
    }

    OGRCoordinateTransformationOptions options;
    if(parms->proj_pipeline)
    {
        /* User specified proj pipeline */
        if(!options.SetCoordinateOperation(parms->proj_pipeline, false))
            throw RunTimeException(CRITICAL, RTE_ERROR, "Failed to set user projlib pipeline");
        mlog(DEBUG, "Set projlib  pipeline: %s", parms->proj_pipeline);
    }

    /* Limit to area of interest if AOI was set */
    bbox_t* aoi  = &parms->aoi_bbox;
    bool useaoi  = !((aoi->lon_min == aoi->lon_max) || (aoi->lat_min == aoi->lat_max));
    if(useaoi)
    {
        if(!options.SetAreaOfInterest(aoi->lon_min, aoi->lat_min, aoi->lon_max, aoi->lat_max))
            throw RunTimeException(CRITICAL, RTE_ERROR, "Failed to set AOI");

        mlog(DEBUG, "Limited projlib extent: (%.2lf, %.2lf) (%.2lf, %.2lf)",
             aoi->lon_min, aoi->lat_min, aoi->lon_max, aoi->lat_max);
    }

    /* Force traditional axis order (lon, lat) */
    targetCRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    sourceCRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    transf = OGRCreateCoordinateTransformation(&sourceCRS, &targetCRS, options);
    if(transf == NULL)
        throw RunTimeException(CRITICAL, RTE_ERROR, "Failed to create coordinates transform");
}

/*----------------------------------------------------------------------------
 * radius2pixels
 *----------------------------------------------------------------------------*/
int GdalRaster::radius2pixels(int _radius) const
{
    /*
     * Code supports only rasters with units in meters (cellSize and radius must be in meters).
     */
    int csize = static_cast<int>(cellSize);

    if(_radius == 0) return 0;
    if(csize == 0) csize = 1;

    int radiusInMeters = ((_radius + csize - 1) / csize) * csize; // Round up to multiples of cell size
    return radiusInMeters / csize;
}

/*----------------------------------------------------------------------------
 * containsWindow
 *----------------------------------------------------------------------------*/
bool GdalRaster::containsWindow(int x, int y, int maxx, int maxy, int windowSize)
{
    if(x < 0 || y < 0)
        return false;

    if((x + windowSize >= maxx) || (y + windowSize >= maxy))
        return false;

    return true;
}

/*----------------------------------------------------------------------------
 * RasterIoWithRetry
 *----------------------------------------------------------------------------*/
void GdalRaster::readRasterWithRetry(int x, int y, int _xsize, int _ysize, void *data, int dataXsize, int dataYsize, GDALRasterIOExtraArg *args)
{
    /*
     * On AWS reading from S3 buckets may result in failed reads due to network issues/timeouts.
     * There is no way to detect this condition based on the error code returned.
     * Because of it, always retry failed read.
     */
    int cnt = 1;
    CPLErr err = CE_None;
    do
    {
        err = band->RasterIO(GF_Read, x, y, _xsize, _ysize, data, dataXsize, dataYsize, GDT_Float64, 0, 0, args);
    } while(err != CE_None && cnt-- && s3sleep());

    if (err != CE_None) throw RunTimeException(CRITICAL, RTE_ERROR, "RasterIO call failed");
}


/*----------------------------------------------------------------------------
 * getRasterSubset
 *----------------------------------------------------------------------------*/
RasterSubset* GdalRaster::getRasterSubset(uint32_t ulx, uint32_t uly, double map_ulx, double map_uly, uint32_t _xsize, uint32_t _ysize)
{
    RasterSubset* subset = NULL;

    try
    {
        GDALDataType dtype = band->GetRasterDataType();
        RecordObject::fieldType_t datatype = RecordObject::INVALID_FIELD;
        switch (dtype) {
            case GDT_Byte:      datatype = RecordObject::UINT8;     break;
            case GDT_Int8:      datatype = RecordObject::UINT8;     break;
            case GDT_UInt16:    datatype = RecordObject::UINT16;    break;
            case GDT_Int16:     datatype = RecordObject::INT16;     break;
            case GDT_UInt32:    datatype = RecordObject::UINT32;    break;
            case GDT_Int32:     datatype = RecordObject::INT32;     break;
            case GDT_UInt64:    datatype = RecordObject::UINT64;    break;
            case GDT_Int64:     datatype = RecordObject::INT64;     break;
            case GDT_Float32:   datatype = RecordObject::FLOAT;     break;
            case GDT_Float64:   datatype = RecordObject::DOUBLE;    break;
            default:            throw RunTimeException(CRITICAL, RTE_ERROR, "Unsupported GDT Datatype: %d", dtype);
        }

        char* wkt;
        targetCRS.exportToWkt(&wkt);

        subset = new RasterSubset(_xsize, _ysize, datatype, map_ulx, map_uly, cellSize, bbox, wkt, gpsTime, fileId);
        CPLFree(wkt);

        if(subset->data)
        {
            int cnt = 1;
            OGRErr err = CE_None;
            do
            {
                err = band->RasterIO(GF_Read, ulx, uly, subset->cols, subset->rows, subset->data, subset->cols, subset->rows, dtype, 0, 0, NULL);
            }
            while(err != CE_None && cnt--);

            if(err != CE_None)
            {
                ssError |= SS_AOI_FAILED_TO_READ_ERROR;
                throw RunTimeException(CRITICAL, RTE_ERROR, "RasterIO call failed: %d", err);
            }

            // mlog(DEBUG, "read %ld bytes (%.1fMB), pixel_ulx: %d, pixel_uly: %d, map_ulx: %.6lf, map_uly: %.6lf, cols2read: %ld, rows2read: %ld, datatype %s\n",
            //      subset->size, (float)subset->size/(1024*1024), ulx, uly, subset->map_ulx, subset->map_uly, subset->cols, subset->rows, GDALGetDataTypeName(dtype));
        }
        else
        {
            ssError |= SS_MEMPOOL_ERROR;
            uint64_t size = _xsize * _ysize * GDALGetDataTypeSizeBytes(dtype);
            mlog(ERROR, "RasterSubset requested memory: %lu MB, available: %lu MB, max: %lu MB", size / (1024*1024),
                RasterSubset::poolsize / (1024*1024),
                RasterSubset::MAX_SIZE / (1024*1024));
        }
    }
    catch (const RunTimeException &e)
    {
        delete subset;
        subset = NULL;
        mlog(e.level(), "Error subsetting: %s", e.what());
    }

    return subset;
}


/*----------------------------------------------------------------------------
 * maptopixel
 *----------------------------------------------------------------------------*/
void GdalRaster::map2pixel(double mapx, double mapy, int& x, int& y)
{
    /* The extra () are needed to keep GCC from optimizing the code and generating wrong results */
    x = floor(invGeoTransform[0] + ((invGeoTransform[1] * mapx) + (invGeoTransform[2] * mapy)));
    y = floor(invGeoTransform[3] + ((invGeoTransform[4] * mapx) + (invGeoTransform[5] * mapy)));
}

/*----------------------------------------------------------------------------
 * pixel2map
 *----------------------------------------------------------------------------*/
void GdalRaster::pixel2map(int x, int y, double& mapx, double& mapy)
{
    double _x = static_cast<double>(x) + 0.5;
    double _y = static_cast<double>(y) + 0.5;

    mapx = (geoTransform[0] + ((geoTransform[1] * _x) + (geoTransform[2] * _y)));
    mapy = (geoTransform[3] + ((geoTransform[4] * _y) + (geoTransform[5] * _y)));
}

