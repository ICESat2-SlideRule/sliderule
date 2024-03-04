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

#ifndef __gdal_raster__
#define __gdal_raster__

/******************************************************************************
 * INCLUDES
 ******************************************************************************/

#include "GeoParms.h"
#include "RasterSample.h"
#include "RasterSubset.h"
#include <ogrsf_frmts.h>
#include <thread>

/******************************************************************************
 * Typedef and macros used by GDAL class
 ******************************************************************************/

#define CHECKPTR(p)                                                           \
do                                                                            \
{                                                                             \
    if ((p) == NULL)                                                          \
    {                                                                         \
        throw RunTimeException(CRITICAL, RTE_ERROR,                           \
              "NULL pointer detected (%s():%d)", __FUNCTION__, __LINE__);     \
    }                                                                         \
} while (0)


#define CHECK_GDALERR(e)                                                      \
do                                                                            \
{                                                                             \
    if ((e))   /* CPLErr and OGRErr types have 0 for no error  */             \
    {                                                                         \
        throw RunTimeException(CRITICAL, RTE_ERROR,                           \
              "GDAL ERROR detected: %d (%s():%d)", e, __FUNCTION__, __LINE__);\
    }                                                                         \
} while (0)



/******************************************************************************
 * GDAL RASTER CLASS
 ******************************************************************************/

class GdalRaster
{
    public:
        /*--------------------------------------------------------------------
         * Constants
         *--------------------------------------------------------------------*/

        static const int MAX_SAMPLING_RADIUS_IN_PIXELS = 50;
        static const int SLIDERULE_EPSG                = 7912;

        /*--------------------------------------------------------------------
         * Typedefs
         *--------------------------------------------------------------------*/

        /*--------------------------------------------------------------------
        * overrideCRS_t
        *     Callback definition for overriding Spatial Reference System.
        *     NOTE: implementation must be thread-safe
        *--------------------------------------------------------------------*/
        typedef OGRErr (*overrideCRS_t)(OGRSpatialReference& crs);

        /* import bbox_t into this namespace from GeoParms.h */
        using bbox_t=GeoParms::bbox_t;

        /*--------------------------------------------------------------------
         * Methods
         *--------------------------------------------------------------------*/

                           GdalRaster     (GeoParms* _parms, const std::string& _fileName, double _gpsTime, uint64_t _fileId, bool _dataIsElevation, overrideCRS_t cb);
        virtual           ~GdalRaster     (void);
        void               open           (void);
        RasterSample*      samplePOI      (OGRPoint* poi, bool dryrun);
        RasterSubset*      subsetAOI      (OGRPolygon* poly);
        RasterSubset*      subsetAOI      (uint32_t ulx, uint32_t uly, uint32_t _xsize, uint32_t _ysize);
        const std::string& getFileName    (void) const { return fileName;}
        int                getRows        (void) const { return ysize; }
        int                getCols        (void) const { return xsize; }
        const bbox_t&      getBbox        (void) const { return bbox; }
        double             getCellSize    (void) const { return cellSize; }
        uint32_t           getSSerror     (void) const { return ssError; }

        /*--------------------------------------------------------------------
         * Static Methods
         *--------------------------------------------------------------------*/

        static void        setCRSfromWkt  (OGRSpatialReference& sref, const char* wkt);
        static std::string getUUID        (void);
        static void        initAwsAccess  (GeoParms* _parms);
        static OGRPolygon  makeRectangle  (double minx, double miny, double maxx, double maxy);
        static bool        ispoint        (const OGRGeometry* geo) { return geo->getGeometryType() == wkbPoint25D; }
        static bool        ispoly         (const OGRGeometry* geo) { return geo->getGeometryType() == wkbPolygon; }

    private:

        /*--------------------------------------------------------------------
        * Data
        *--------------------------------------------------------------------*/

        GeoParms*      parms;
        double         gpsTime;  /* Time the raster data was collected and/or generated */
        uint64_t       fileId;   /* unique identifier of raster file used for downstream processing */

        OGRCoordinateTransformation* transf;
        OGRSpatialReference sourceCRS;
        OGRSpatialReference targetCRS;
        overrideCRS_t       overrideCRS;

        std::string     fileName;
        GDALDataset    *dset;
        GDALRasterBand* band;
        bool            dataIsElevation;
        uint32_t        xsize;
        uint32_t        ysize;
        double          cellSize;
        bbox_t          bbox;
        uint32_t        radiusInPixels;
        double          geoTransform[6];
        double          invGeoTransform[6];
        uint32_t        ssError;

        /*--------------------------------------------------------------------
        * Methods
        *--------------------------------------------------------------------*/

        void        readPixel           (const OGRPoint* poi, RasterSample* sample);
        void        resamplePixel       (const OGRPoint* poi, RasterSample* sample);
        void        computeZonalStats   (const OGRPoint* poi, RasterSample* sample);
        inline bool nodataCheck         (RasterSample* sample);
        void        createTransform     (void);
        int         radius2pixels       (int _radius) const;
        static inline bool containsWindow (int x, int y, int maxx, int maxy, int windowSize);
        inline void readRasterWithRetry (int x, int y, int xsize, int ysize, void* data, int dataXsize, int dataYsize, GDALRasterIOExtraArg* args);
        RasterSubset* getRasterSubset   (uint32_t ulx, uint32_t uly, double map_ulx, double map_uly, uint32_t _xsize, uint32_t _ysize);

        void        map2pixel           (double mapx, double mapy, int& x, int& y);
        void        map2pixel           (const OGRPoint* poi, int& x, int& y) { map2pixel(poi->getX(), poi->getY(), x, y); }
        void        pixel2map           (int x, int y, double& mapx, double& mapy);

        static bool s3sleep(void) {std::this_thread::sleep_for(std::chrono::milliseconds(50)); return true; }
};

#endif  /* __gdal_raster__ */
