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

#ifndef __geo_indexed_raster__
#define __geo_indexed_raster__

/******************************************************************************
 * INCLUDES
 ******************************************************************************/

#include "GdalRaster.h"
#include "RasterObject.h"
#include "Ordering.h"


/******************************************************************************
 * GEO RASTER CLASS
 ******************************************************************************/

class GeoIndexedRaster: public RasterObject
{
    public:

        /*--------------------------------------------------------------------
         * Constants
         *--------------------------------------------------------------------*/

        static const int   MAX_READER_THREADS = 200;

        static const char* FLAGS_TAG;
        static const char* VALUE_TAG;

        /*--------------------------------------------------------------------
         * Typedefs
         *--------------------------------------------------------------------*/

        typedef struct {
            bool         dataIsElevation;
            std::string  tag;
            std::string  fileName;
        } raster_info_t;

        typedef struct {
            std::string                id;
            std::vector<raster_info_t> infovect;
            TimeLib::gmt_time_t        gmtDate;
            int64_t                    gpsTime;
        } rasters_group_t;

        typedef struct CacheItem {
            bool            enabled;
            RasterSample*   sample;
            RasterSubset*   subset;
            GdalRaster*     raster;
            ~CacheItem(void) {delete raster;}
        } cacheitem_t;

        typedef struct Reader {
            GeoIndexedRaster*   obj;
            OGRGeometry*        geo;
            Thread*             thread;
            cacheitem_t*        entry;
            Cond                sync;
            bool                run;
            explicit Reader(GeoIndexedRaster* raster);
            ~Reader(void);
        } reader_t;

        /*--------------------------------------------------------------------
         * Methods
         *--------------------------------------------------------------------*/

        static void     init              (void);
        static void     deinit            (void);
        uint32_t        getSamples        (OGRGeometry* geo, int64_t gps, std::vector<RasterSample*>& slist, bool dryrun=false, void* param=NULL) final;
        uint32_t        getSubsets        (OGRGeometry* geo, int64_t gps, std::vector<RasterSubset*>& slist, void* param=NULL) final;
        virtual        ~GeoIndexedRaster  (void);

    protected:

        /*--------------------------------------------------------------------
         * Methods
         *--------------------------------------------------------------------*/

                        GeoIndexedRaster      (lua_State* L, GeoParms* _parms, GdalRaster::overrideCRS_t cb=NULL);
        virtual void    getGroupSamples       (const rasters_group_t* rgroup, std::vector<RasterSample*>& slist, uint32_t flags);
        virtual void    getGroupSubsets       (const rasters_group_t* rgroup, std::vector<RasterSubset*>& slist);
        uint32_t        getGroupFlags         (const rasters_group_t* rgroup);
        static double   getGmtDate            (const OGRFeature* feature, const char* field,  TimeLib::gmt_time_t& gmtDate);
        virtual bool    openGeoIndex          (const OGRGeometry* geo);
        virtual void    getIndexFile          (const OGRGeometry* geo, std::string& file) = 0;
        virtual bool    findRasters           (const OGRGeometry* geo) = 0;
        void            sampleRasters         (OGRGeometry* geo);
        bool            sample                (OGRGeometry* geo, int64_t gps);
        void            emptyFeaturesList     (void);

        /*--------------------------------------------------------------------
         * Types
         *--------------------------------------------------------------------*/

        typedef Ordering<rasters_group_t*, unsigned long> GroupOrdering;
        typedef Dictionary<cacheitem_t*> CacheDictionary;

        /*--------------------------------------------------------------------
         * Data
         *--------------------------------------------------------------------*/

        Mutex                   samplingMutex;
        GroupOrdering           groupList;
        CacheDictionary         cache;
        vector<OGRFeature*>     featuresList;
        OGRPolygon              geoIndexPoly;
        uint32_t                ssError;

    private:

        /*--------------------------------------------------------------------
         * Constants
         *--------------------------------------------------------------------*/

        static const int DATA_TO_SAMPLE   = 0;
        static const int DATA_SAMPLED     = 1;
        static const int NUM_SYNC_SIGNALS = 2;

        /*--------------------------------------------------------------------
         * Data
         *--------------------------------------------------------------------*/

        List<reader_t*>           readers;
        GdalRaster::overrideCRS_t crscb;

        std::string               indexFile;
        GdalRaster::bbox_t        bbox;
        uint32_t                  rows;
        uint32_t                  cols;

        std::atomic_bool          dryrun;

        /*--------------------------------------------------------------------
         * Methods
         *--------------------------------------------------------------------*/

        static int      luaDimensions   (lua_State* L);
        static int      luaBoundingBox  (lua_State* L);
        static int      luaCellSize     (lua_State* L);

        static void*    readingThread   (void *param);

        void            createThreads   (void);
        bool            updateCache     (void);
        bool            filterRasters   (int64_t gps);
};

#endif  /* __geo_indexed_raster__ */
