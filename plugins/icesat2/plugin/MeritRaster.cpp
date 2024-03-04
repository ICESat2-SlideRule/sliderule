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
#include "core.h"
#include "h5.h"
#include "geo.h"
#include "MeritRaster.h"

#include <math.h>

/******************************************************************************
 * STATIC DATA
 ******************************************************************************/

const char* MeritRaster::ASSET_NAME = "merit-dem";
const char* MeritRaster::RESOURCE_NAME = "merit_3as_20200617_001_01.h5";

const double MeritRaster::X_SCALE = (1.0 / 1200.0);
const double MeritRaster::Y_SCALE = (-1.0 / 1200.0);

/******************************************************************************
 * PUBLIC METHODS
 ******************************************************************************/

/*----------------------------------------------------------------------------
 * init
 *----------------------------------------------------------------------------*/
void MeritRaster::init(void)
{
}

/*----------------------------------------------------------------------------
 * create
 *----------------------------------------------------------------------------*/
RasterObject* MeritRaster::create(lua_State* L, GeoParms* _parms)
{
    return new MeritRaster(L, _parms);
}

/*----------------------------------------------------------------------------
 * Destructor
 *----------------------------------------------------------------------------*/
MeritRaster::~MeritRaster(void)
{
    delete [] cache;
    if(asset) asset->releaseLuaObject();
}

/******************************************************************************
 * PROTECTED METHODS
 ******************************************************************************/

/*----------------------------------------------------------------------------
 * Constructor
 *----------------------------------------------------------------------------*/
MeritRaster::MeritRaster(lua_State *L, GeoParms* _parms):
    RasterObject(L, _parms),
    cacheLat(0),
    cacheLon(0),
    cache(NULL),
    asset(NULL)
{
    /* Initialize Time */
    TimeLib::gmt_time_t gmt_date = {
        .year = 2020,
        .doy = 169,
        .hour = 0,
        .minute = 0,
        .second = 0,
        .millisecond = 0
    };
    gpsTime = TimeLib::gmt2gpstime(gmt_date);

    /*  Inintialize Asset */
    asset = dynamic_cast<Asset*>(LuaObject::getLuaObjectByName(ASSET_NAME, Asset::OBJECT_TYPE));
    if(!asset) throw RunTimeException(CRITICAL, RTE_ERROR, "Unable to find asset %s", ASSET_NAME);
}

/*----------------------------------------------------------------------------
 * getSamples
 *----------------------------------------------------------------------------*/
uint32_t MeritRaster::getSamples (OGRGeometry* geo, int64_t gps, std::vector<RasterSample*>& slist, bool dryrun, void* param)
{
    (void)param;
    (void)gps;

    if(dryrun)
    {
        RasterSample* sample = new RasterSample(((double)gpsTime / (double)1000.0), 0);
        slist.push_back(sample);
        return SS_NO_ERRORS;
    }

    OGRPoint* poi = geo->toPoint();

    /* Determine Upper Left Coordinates */
    int left_lon = ((int)floor(poi->getX() / 5.0)) * 5;
    int upper_lat = ((int)ceil(poi->getY() / 5.0)) * 5;

    /* Calculate Pixel Location */
    int x_offset = (int)(((double)poi->getX() - left_lon) / X_SCALE);
    int y_offset = (int)(((double)poi->getY() - upper_lat) / Y_SCALE);

    /* Check Pixel Location */
    if( x_offset < 0 || x_offset >= X_MAX ||
        y_offset < 0 || y_offset >= Y_MAX )
    {
        mlog(ERROR, "Invalid pixel location for MERIT DEM at %lf, %lf: %d, %d\n", poi->getX(), poi->getY(), x_offset, y_offset);
        return SS_POI_OUT_OF_BOUNDS_ERROR;
    }

    /* Handle Negative Longitudes */
    char char4lon = 'e';
    if(left_lon < 0)
    {
        char4lon = 'w';
        left_lon *= -1;
    }

    /* Handle Negative Latitudes */
    char char4lat = 'n';
    if(upper_lat < 0)
    {
        char4lon = 's';
        upper_lat *= -1;
    }

    /* Build Dataset Name */
    FString dataset("%c%02d%c%03d_MERITdem_wgs84", char4lat, upper_lat, char4lon, left_lon);

    try
    {
        double value = 0.0;
        bool value_cached = false;

        /* Check Cache */
        cacheMut.lock();
        {
            if(cache && (left_lon == cacheLon) && (upper_lat == cacheLat))
            {
                value = (double)cache[(y_offset *  X_MAX) + x_offset];
                value_cached = true;
            }
        }
        cacheMut.unlock();

        /* Read Dataset */
        if(!value_cached)
        {
            H5Coro::context_t context;
            H5Coro::info_t info = H5Coro::read(asset, RESOURCE_NAME, dataset.c_str(), RecordObject::DYNAMIC, H5Coro::ALL_COLS, 0, H5Coro::ALL_ROWS, &context, false, traceId);
            assert(info.datasize == (X_MAX * Y_MAX * sizeof(int32_t)));
            int32_t* tile = (int32_t*)info.data;

            /* Update Cache */
            cacheMut.lock();
            {
                delete [] cache;
                cache = tile;
                cacheLon = left_lon;
                cacheLat = upper_lat;
            }
            cacheMut.unlock();

            /* Read Value */
            value = (double)tile[(y_offset *  X_MAX) + x_offset];
        }

        /* Build Sample */
        RasterSample* sample = new RasterSample(((double)gpsTime / (double)1000.0), 0);
        sample->value = value;

        /* Return Sample */
        slist.push_back(sample);
    }
    catch(const RunTimeException& e)
    {
        mlog(ERROR, "Failed to sample dataset: %s", dataset.c_str());
    }

    return SS_NO_ERRORS;
}

/*----------------------------------------------------------------------------
 * getSubset
 *----------------------------------------------------------------------------*/
uint32_t MeritRaster::getSubsets(OGRGeometry* geo, int64_t gps, std::vector<RasterSubset*>& slist, void* param)
{
    std::ignore = geo;
    std::ignore = gps;
    std::ignore = slist;
    std::ignore = param;
    return SS_NO_ERRORS;
}

