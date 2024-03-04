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

#ifndef __geo_raster__
#define __geo_raster__

/******************************************************************************
 * INCLUDES
 ******************************************************************************/

#include "GdalRaster.h"
#include "RasterObject.h"

/******************************************************************************
 * GEO RASTER CLASS
 ******************************************************************************/

class GeoRaster: public RasterObject
{
    public:

        /*--------------------------------------------------------------------
         * Methods
         *--------------------------------------------------------------------*/

        virtual      ~GeoRaster  (void);
        uint32_t      getSamples (OGRGeometry* geo, int64_t gps, std::vector<RasterSample*>& slist, bool dryrun=false, void* param=NULL) final;
        uint32_t      getSubsets (OGRGeometry* geo, int64_t gps, std::vector<RasterSubset*>& slist, void* param=NULL) final;
        uint32_t      getPixels  (uint32_t ulx, uint32_t uly, uint32_t xsize, uint32_t ysize, std::vector<RasterSubset*>& slist, void* param=NULL) override;

    protected:

        /*--------------------------------------------------------------------
         * Methods
         *--------------------------------------------------------------------*/

         GeoRaster  (lua_State* L, GeoParms* _parms, const std::string& _fileName, double _gpsTime, bool dataIsElevation, GdalRaster::overrideCRS_t cb=NULL);

         std::string getFileName(void)
         {
             return raster.getFileName();
         }

    private:

        /*--------------------------------------------------------------------
        * Data
        *--------------------------------------------------------------------*/

        Mutex      samplingMutex;
        GdalRaster raster;

        /*--------------------------------------------------------------------
        * Methods
        *--------------------------------------------------------------------*/

        static int luaDimensions(lua_State* L);
        static int luaBoundingBox(lua_State* L);
        static int luaCellSize(lua_State* L);
};

#endif  /* __geo_raster__ */
