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

#ifndef __geo_lib__
#define __geo_lib__

#include "LuaEngine.h"
#include "MathLib.h"

class GeoLib: public MathLib
{
    public:

        /*--------------------------------------------------------------------
         * Constants
         *--------------------------------------------------------------------*/

        static const char* DEFAULT_CRS;

        /*--------------------------------------------------------------------
         * UTMTransform Subclass
         *--------------------------------------------------------------------*/

        class UTMTransform
        {
            public:
                UTMTransform(double initial_latitude, double initial_longitude, const char* input_crs=DEFAULT_CRS);
                ~UTMTransform(void);
                point_t calculateCoordinates(double latitude, double longitude);
                int zone;
                bool is_north;
                bool in_error;
            private:
                typedef void* utm_transform_t;
                utm_transform_t transform;
        };

        /*--------------------------------------------------------------------
         * TIFFImage Subclass
         *--------------------------------------------------------------------*/

        class TIFFImage
        {
            public:
                explicit TIFFImage(const char* filename);
                ~TIFFImage(void);
                uint32_t getPixel(uint32_t x, uint32_t y);
            private:
                uint32_t width;
                uint32_t length;
                uint32_t* raster;
        };

        /*--------------------------------------------------------------------
         * Methods
         *--------------------------------------------------------------------*/

        static int luaCalcUTM (lua_State* L);
};

#endif /* __geo_lib__ */
