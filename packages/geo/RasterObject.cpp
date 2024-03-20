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

#include "OsApi.h"
#include "core.h"
#include "RasterObject.h"
#include "GdalRaster.h"
#include "GeoIndexedRaster.h"
#include "Ordering.h"
#include "List.h"

#include <arrow/builder.h>
#include <arrow/table.h>
#include <arrow/io/file.h>
#include <arrow/util/key_value_metadata.h>
#include <parquet/arrow/writer.h>
#include <parquet/arrow/schema.h>
#include <parquet/properties.h>
#include <parquet/file_writer.h>
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <arrow/ipc/api.h>
#include <parquet/arrow/reader.h>
#include <arrow/status.h>
#include <arrow/result.h>
#include <arrow/array.h>
#include <iostream>
#include <memory>

#include <cstring>   // For memcpy
#include <stdexcept> // For runtime_error
#include <string>
#include <endian.h> // For htobe32, htole32, be32toh, le32toh
#include <cstdint>  // For fixed-width integer types


#ifdef __aws__
#include "aws.h"
#endif

/******************************************************************************
 * STATIC DATA
 ******************************************************************************/

const char* RasterObject::OBJECT_TYPE  = "RasterObject";
const char* RasterObject::LUA_META_NAME  = "RasterObject";
const struct luaL_Reg RasterObject::LUA_META_TABLE[] = {
    {NULL,          NULL}
};

Mutex RasterObject::factoryMut;
Dictionary<RasterObject::factory_t> RasterObject::factories;

/******************************************************************************
 * PUBLIC METHODS
 ******************************************************************************/

/*----------------------------------------------------------------------------
 * init
 *----------------------------------------------------------------------------*/
void RasterObject::init( void )
{
}

/*----------------------------------------------------------------------------
 * deinit
 *----------------------------------------------------------------------------*/
void RasterObject::deinit( void )
{
}

/*----------------------------------------------------------------------------
 * luaCreate
 *----------------------------------------------------------------------------*/
int RasterObject::luaCreate( lua_State* L )
{
    GeoParms* _parms = NULL;
    try
    {
        /* Get Parameters */
        _parms = dynamic_cast<GeoParms*>(getLuaObject(L, 1, GeoParms::OBJECT_TYPE));
        if(_parms == NULL) throw RunTimeException(CRITICAL, RTE_ERROR, "Failed to create GeoParms object");

        /* Get Factory */
        factory_t factory;
        bool found = false;
        factoryMut.lock();
        {
            found = factories.find(_parms->asset_name, &factory);
        }
        factoryMut.unlock();

        /* Check Factory */
        if(!found) throw RunTimeException(CRITICAL, RTE_ERROR, "Failed to find registered raster for %s", _parms->asset_name);

        /* Create Raster */
        RasterObject* _raster = factory.create(L, _parms);
        if(_raster == NULL) throw RunTimeException(CRITICAL, RTE_ERROR, "Failed to create raster of type: %s", _parms->asset_name);

        /* Return Object */
        return createLuaObject(L, _raster);
    }
    catch(const RunTimeException& e)
    {
        if(_parms) _parms->releaseLuaObject();
        mlog(e.level(), "Error creating %s: %s", LUA_META_NAME, e.what());
        return returnLuaStatus(L, false);
    }
}

/*----------------------------------------------------------------------------
 * registerDriver
 *----------------------------------------------------------------------------*/
bool RasterObject::registerRaster (const char* _name, factory_f create)
{
    bool status;

    factoryMut.lock();
    {
        factory_t factory = { .create = create };
        status = factories.add(_name, factory);
    }
    factoryMut.unlock();

    return status;
}

/*----------------------------------------------------------------------------
 * getPixels
 *----------------------------------------------------------------------------*/
uint32_t RasterObject::getPixels(uint32_t ulx, uint32_t uly, uint32_t xsize, uint32_t ysize, std::vector<RasterSubset*>& slist, void* param)
{
    std::ignore = ulx;
    std::ignore = uly;
    std::ignore = xsize;
    std::ignore = ysize;
    std::ignore = slist;
    std::ignore = param;

    return 0;
}







/*
 ******************************************************************************
 * Code for sampling rasters from parquet files
 ******************************************************************************
 */
typedef struct {
     uint8_t                 byteOrder;
     uint32_t                wkbType;
     double                  x;
     double                  y;
} ALIGN_PACKED wkbpoint_t;


double swap_double(const double value)
{
    union {
        uint64_t i;
        double   d;
    } conv;

    conv.d = value;
    conv.i = __bswap_64(conv.i);
    return conv.d;
}

OGRPoint ConvertWKBToPoint(const std::string& wkb_data)
{
    wkbpoint_t point;

    if(wkb_data.size() < sizeof(wkbpoint_t))
    {
        throw std::runtime_error("Invalid WKB data size.");
    }

    // Byte order is the first byte
    size_t offset = 0;
    std::memcpy(&point.byteOrder, wkb_data.data() + offset, sizeof(uint8_t));
    offset += sizeof(uint8_t);

    // Next four bytes are wkbType
    std::memcpy(&point.wkbType, wkb_data.data() + offset, sizeof(uint32_t));

    // Convert to host byte order if necessary
    if(point.byteOrder == 0)
    {
        // Big endian
        point.wkbType = be32toh(point.wkbType);
    }
    else if(point.byteOrder == 1)
    {
        // Little endian
        point.wkbType = le32toh(point.wkbType);
    }
    else
    {
        throw std::runtime_error("Unknown byte order.");
    }
    offset += sizeof(uint32_t);

    // Next eight bytes are x coordinate
    std::memcpy(&point.x, wkb_data.data() + offset, sizeof(double));
    if((point.byteOrder == 0 && __BYTE_ORDER != __BIG_ENDIAN) ||
       (point.byteOrder == 1 && __BYTE_ORDER != __LITTLE_ENDIAN))
    {
        point.x = swap_double(point.x);
    }
    offset += sizeof(double);

    // Next eight bytes are y coordinate
    std::memcpy(&point.y, wkb_data.data() + offset, sizeof(double));
    if((point.byteOrder == 0 && __BYTE_ORDER != __BIG_ENDIAN) ||
       (point.byteOrder == 1 && __BYTE_ORDER != __LITTLE_ENDIAN))
    {
        point.y = swap_double(point.y);
    }

    OGRPoint poi(point.x, point.y, 0);
    return poi;
}



std::shared_ptr<arrow::Table> ReadParquetFileToTable(const std::string& file_path)
{
    std::shared_ptr<arrow::io::ReadableFile> infile;
    PARQUET_ASSIGN_OR_THROW(infile, arrow::io::ReadableFile::Open(file_path, arrow::default_memory_pool()));

    std::unique_ptr<parquet::arrow::FileReader> reader;
    PARQUET_THROW_NOT_OK(parquet::arrow::OpenFile(infile, arrow::default_memory_pool(), &reader));

    std::shared_ptr<arrow::Table> table;
    PARQUET_THROW_NOT_OK(reader->ReadTable(&table));

    return table;
}


void WriteTableToParquetFile(std::shared_ptr<arrow::Table> table, const std::string& file_path)
{
    std::shared_ptr<arrow::io::FileOutputStream> outfile;
    PARQUET_ASSIGN_OR_THROW(outfile, arrow::io::FileOutputStream::Open(file_path));

    /* Create a Parquet writer properties builder */
    parquet::WriterProperties::Builder writer_props_builder;
    writer_props_builder.compression(parquet::Compression::SNAPPY);
    writer_props_builder.version(parquet::ParquetVersion::PARQUET_2_6);
    shared_ptr<parquet::WriterProperties> writer_properties = writer_props_builder.build();

    /* Create an Arrow writer properties builder to specify that we want to store Arrow schema */
    auto arrow_properties = parquet::ArrowWriterProperties::Builder().store_schema()->build();

    PARQUET_THROW_NOT_OK(parquet::arrow::WriteTable(*table, arrow::default_memory_pool(), outfile, table->num_rows(), writer_properties, arrow_properties));
}


void InspectArrowListArray(const std::shared_ptr<arrow::Array>& array, int64_t max_rows = 5)
{
    /* Ensure the input array is actually a ListArray */
    if(array->type_id() != arrow::Type::LIST)
    {
        std::cerr << "Error: Array is not a ListArray" << std::endl;
        return;
    }

    auto list_array = std::static_pointer_cast<arrow::ListArray>(array);

    std::cout << "Inspecting ListArray data:" << std::endl;
    int64_t max_rows_to_print = std::min(max_rows, list_array->length());
    for (int i = 0; i < max_rows_to_print; ++i)
    {
        /* Slice the underlying values array to get the sublist for this row */
        std::shared_ptr<arrow::Array> values_array = list_array->values()->Slice(list_array->value_offset(i), list_array->value_length(i));
        PARQUET_THROW_NOT_OK(arrow::PrettyPrint(*values_array, 0, &std::cout));
        std::cout << std::endl;
    }
}




void PrintMetadata(const std::string& file_path)
{
    std::shared_ptr<arrow::io::ReadableFile> infile;
    PARQUET_ASSIGN_OR_THROW(infile, arrow::io::ReadableFile::Open(file_path, arrow::default_memory_pool()));

    std::unique_ptr<parquet::arrow::FileReader> parquet_reader;
    PARQUET_THROW_NOT_OK(parquet::arrow::OpenFile(infile, arrow::default_memory_pool(), &parquet_reader));

    std::shared_ptr<arrow::Table> table;
    PARQUET_THROW_NOT_OK(parquet_reader->ReadTable(&table));

    if (table->schema()->metadata())
    {
        std::shared_ptr<const arrow::KeyValueMetadata> metadata = table->schema()->metadata();
        for (int i = 0; i < metadata->size(); ++i)
        {
            std::cout << "Key: " << metadata->key(i) << ", Value: " << metadata->value(i) << std::endl;
            std::cout << std::endl;
        }
    }
    else std::cout << "No metadata found in the table." << std::endl;
}


/*----------------------------------------------------------------------------
 * getSamples
 *----------------------------------------------------------------------------*/
uint32_t RasterObject::getSamples(const std::string& in_file, const std::string& out_file, const std::string& prefix, const int64_t gps, void* param)
{
    std::ignore = param;
    std::vector<RasterSample*> slist;

    try
    {
        auto pool = arrow::default_memory_pool();
        std::shared_ptr<arrow::Table> table = ReadParquetFileToTable(in_file);

        /* Create list builders for the new columns in RasterSample */
        arrow::ListBuilder value_list_builder(pool, std::make_shared<arrow::DoubleBuilder>());
        auto value_builder = static_cast<arrow::DoubleBuilder*>(value_list_builder.value_builder());

        arrow::ListBuilder time_list_builder(pool, std::make_shared<arrow::DoubleBuilder>());
        auto time_builder = static_cast<arrow::DoubleBuilder*>(time_list_builder.value_builder());

        arrow::ListBuilder flags_list_builder(pool, std::make_shared<arrow::UInt32Builder>());
        auto flags_builder = static_cast<arrow::UInt32Builder*>(flags_list_builder.value_builder());

        arrow::ListBuilder fileid_list_builder(pool, std::make_shared<arrow::UInt64Builder>());
        auto fileid_builder = static_cast<arrow::UInt64Builder*>(fileid_list_builder.value_builder());

        /* Create list builders for zonal stats */
        arrow::ListBuilder count_list_builder(pool, std::make_shared<arrow::UInt32Builder>());
        auto count_builder = static_cast<arrow::UInt32Builder*>(count_list_builder.value_builder());

        arrow::ListBuilder min_list_builder(pool, std::make_shared<arrow::DoubleBuilder>());
        auto min_builder = static_cast<arrow::DoubleBuilder*>(min_list_builder.value_builder());

        arrow::ListBuilder max_list_builder(pool, std::make_shared<arrow::DoubleBuilder>());
        auto max_builder = static_cast<arrow::DoubleBuilder*>(max_list_builder.value_builder());

        arrow::ListBuilder mean_list_builder(pool, std::make_shared<arrow::DoubleBuilder>());
        auto mean_builder = static_cast<arrow::DoubleBuilder*>(mean_list_builder.value_builder());

        arrow::ListBuilder median_list_builder(pool, std::make_shared<arrow::DoubleBuilder>());
        auto median_builder = static_cast<arrow::DoubleBuilder*>(median_list_builder.value_builder());

        arrow::ListBuilder stdev_list_builder(pool, std::make_shared<arrow::DoubleBuilder>());
        auto stdev_builder = static_cast<arrow::DoubleBuilder*>(stdev_list_builder.value_builder());

        arrow::ListBuilder mad_list_builder(pool, std::make_shared<arrow::DoubleBuilder>());
        auto mad_builder = static_cast<arrow::DoubleBuilder*>(mad_list_builder.value_builder());

        /* Reserve memory for the new columns */
        PARQUET_THROW_NOT_OK(value_list_builder.Reserve(table->num_rows()));
        PARQUET_THROW_NOT_OK(time_list_builder.Reserve(table->num_rows()));
        PARQUET_THROW_NOT_OK(flags_list_builder.Reserve(table->num_rows()));
        PARQUET_THROW_NOT_OK(fileid_list_builder.Reserve(table->num_rows()));

        if(hasZonalStats())
        {
            PARQUET_THROW_NOT_OK(count_list_builder.Reserve(table->num_rows()));
            PARQUET_THROW_NOT_OK(min_list_builder.Reserve(table->num_rows()));
            PARQUET_THROW_NOT_OK(max_list_builder.Reserve(table->num_rows()));
            PARQUET_THROW_NOT_OK(mean_list_builder.Reserve(table->num_rows()));
            PARQUET_THROW_NOT_OK(median_list_builder.Reserve(table->num_rows()));
            PARQUET_THROW_NOT_OK(stdev_list_builder.Reserve(table->num_rows()));
            PARQUET_THROW_NOT_OK(mad_list_builder.Reserve(table->num_rows()));
        }


        auto geometry_column_index = table->schema()->GetFieldIndex("geometry");
        if(geometry_column_index == -1)
        {
            throw RunTimeException(ERROR, RTE_ERROR, "Geometry column not found.");
        }

        auto geometry_column = std::static_pointer_cast<arrow::BinaryArray>(table->column(geometry_column_index)->chunk(0));
        mlog(DEBUG, "Geometry column elements: %ld", geometry_column->length());

        /* The geometry column is binary type */
        auto binary_array = std::static_pointer_cast<arrow::BinaryArray>(geometry_column);

        /* Iterate over each item in the geometry column */
        for(int64_t item = 0; item < binary_array->length(); item++)
        {
            auto wkb_data = binary_array->GetString(item);     /* Get WKB data as string (binary data) */
            OGRPoint poi = ConvertWKBToPoint(wkb_data);

            bool listvalid = true;
            uint32_t err = getSamples(&poi, gps, slist, NULL);

            if(err & SS_THREADS_LIMIT_ERROR)
            {
                listvalid = false;
                mlog(CRITICAL, "Too many rasters to sample, max allowed: %d, limit your AOI/temporal range or use filters", GeoIndexedRaster::MAX_READER_THREADS);
            }

            /* Start a new lists for each RasterSample */
            PARQUET_THROW_NOT_OK(value_list_builder.Append());
            PARQUET_THROW_NOT_OK(time_list_builder.Append());
            PARQUET_THROW_NOT_OK(flags_list_builder.Append());
            PARQUET_THROW_NOT_OK(fileid_list_builder.Append());

            if(hasZonalStats())
            {
                PARQUET_THROW_NOT_OK(count_list_builder.Append());
                PARQUET_THROW_NOT_OK(min_list_builder.Append());
                PARQUET_THROW_NOT_OK(max_list_builder.Append());
                PARQUET_THROW_NOT_OK(mean_list_builder.Append());
                PARQUET_THROW_NOT_OK(median_list_builder.Append());
                PARQUET_THROW_NOT_OK(stdev_list_builder.Append());
                PARQUET_THROW_NOT_OK(mad_list_builder.Append());
            }

            /* Populate samples */
            if(listvalid && !slist.empty())
            {
                for(uint32_t i = 0; i < slist.size(); i++)
                {
                    const RasterSample* sample = slist[i];

                    PARQUET_THROW_NOT_OK(value_builder->Append(sample->value));
                    PARQUET_THROW_NOT_OK(time_builder->Append(sample->time));
                    PARQUET_THROW_NOT_OK(flags_builder->Append(sample->flags));
                    PARQUET_THROW_NOT_OK(fileid_builder->Append(sample->fileId));

                    if(hasZonalStats())
                    {
                        PARQUET_THROW_NOT_OK(count_builder->Append(sample->stats.count));
                        PARQUET_THROW_NOT_OK(min_builder->Append(sample->stats.min));
                        PARQUET_THROW_NOT_OK(max_builder->Append(sample->stats.max));
                        PARQUET_THROW_NOT_OK(mean_builder->Append(sample->stats.mean));
                        PARQUET_THROW_NOT_OK(median_builder->Append(sample->stats.median));
                        PARQUET_THROW_NOT_OK(stdev_builder->Append(sample->stats.stdev));
                        PARQUET_THROW_NOT_OK(mad_builder->Append(sample->stats.mad));
                    }
                }

            }
            else mlog(DEBUG, "No samples read for (%.2lf, %.2lf)", poi.getX(), poi.getY());

            /* Free samples */
            for(const RasterSample* sample : slist)
                delete sample;

            slist.clear();
        }

        /* Finalize the lists */
        std::shared_ptr<arrow::Array> value_list_array, time_list_array, fileid_list_array, flags_list_array;
        std::shared_ptr<arrow::Array> count_list_array, min_list_array, max_list_array, mean_list_array, median_list_array, stdev_list_array, mad_list_array;

        PARQUET_THROW_NOT_OK(value_list_builder.Finish(&value_list_array));
        PARQUET_THROW_NOT_OK(time_list_builder.Finish(&time_list_array));
        PARQUET_THROW_NOT_OK(flags_list_builder.Finish(&flags_list_array));
        PARQUET_THROW_NOT_OK(fileid_list_builder.Finish(&fileid_list_array));

        if(hasZonalStats())
        {
            PARQUET_THROW_NOT_OK(count_list_builder.Finish(&count_list_array));
            PARQUET_THROW_NOT_OK(min_list_builder.Finish(&min_list_array));
            PARQUET_THROW_NOT_OK(max_list_builder.Finish(&max_list_array));
            PARQUET_THROW_NOT_OK(mean_list_builder.Finish(&mean_list_array));
            PARQUET_THROW_NOT_OK(median_list_builder.Finish(&median_list_array));
            PARQUET_THROW_NOT_OK(stdev_list_builder.Finish(&stdev_list_array));
            PARQUET_THROW_NOT_OK(mad_list_builder.Finish(&mad_list_array));
        }

        // InspectArrowListArray(min_list_array);
        // InspectArrowListArray(max_list_array);

        /* Create fields for the new columns */
        auto value_field  = std::make_shared<arrow::Field>(prefix + ".value",  arrow::list(arrow::float64()));
        auto time_field   = std::make_shared<arrow::Field>(prefix + ".time",   arrow::list(arrow::float64()));
        auto flags_field  = std::make_shared<arrow::Field>(prefix + ".flags",  arrow::list(arrow::uint32()));
        auto fileid_field = std::make_shared<arrow::Field>(prefix + ".fileid", arrow::list(arrow::uint64()));

        auto count_field  = std::make_shared<arrow::Field>(prefix + ".stats.count",  arrow::list(arrow::uint32()));
        auto min_field    = std::make_shared<arrow::Field>(prefix + ".stats.min",    arrow::list(arrow::float64()));
        auto max_field    = std::make_shared<arrow::Field>(prefix + ".stats.max",    arrow::list(arrow::float64()));
        auto mean_field   = std::make_shared<arrow::Field>(prefix + ".stats.mean",   arrow::list(arrow::float64()));
        auto median_field = std::make_shared<arrow::Field>(prefix + ".stats.median", arrow::list(arrow::float64()));
        auto stdev_field  = std::make_shared<arrow::Field>(prefix + ".stats.stdev",  arrow::list(arrow::float64()));
        auto mad_field    = std::make_shared<arrow::Field>(prefix + ".stats.mad",    arrow::list(arrow::float64()));

        /* Create new schema by combining the existing table schema with new fields */
        std::vector<std::shared_ptr<arrow::Field>> fields = table->schema()->fields();
        fields.push_back(value_field);
        fields.push_back(time_field);
        fields.push_back(flags_field);
        fields.push_back(fileid_field);
        if(hasZonalStats())
        {
            fields.push_back(count_field);
            fields.push_back(min_field);
            fields.push_back(max_field);
            fields.push_back(mean_field);
            fields.push_back(median_field);
            fields.push_back(stdev_field);
            fields.push_back(mad_field);
        }
        auto combined_schema = std::make_shared<arrow::Schema>(fields);

        /* Combine existing column data with new column data */
        std::vector<std::shared_ptr<arrow::ChunkedArray>> columns = table->columns();
        columns.push_back(std::make_shared<arrow::ChunkedArray>(value_list_array));
        columns.push_back(std::make_shared<arrow::ChunkedArray>(time_list_array));
        columns.push_back(std::make_shared<arrow::ChunkedArray>(flags_list_array));
        columns.push_back(std::make_shared<arrow::ChunkedArray>(fileid_list_array));
        if(hasZonalStats())
        {
            columns.push_back(std::make_shared<arrow::ChunkedArray>(count_list_array));
            columns.push_back(std::make_shared<arrow::ChunkedArray>(min_list_array));
            columns.push_back(std::make_shared<arrow::ChunkedArray>(max_list_array));
            columns.push_back(std::make_shared<arrow::ChunkedArray>(mean_list_array));
            columns.push_back(std::make_shared<arrow::ChunkedArray>(median_list_array));
            columns.push_back(std::make_shared<arrow::ChunkedArray>(stdev_list_array));
            columns.push_back(std::make_shared<arrow::ChunkedArray>(mad_list_array));
        }


        /* Attach metadata to the new schema */
        auto metadata = table->schema()->metadata();
        combined_schema = combined_schema->WithMetadata(metadata);

        /* Create a new table with the combined schema and columns */
        auto updated_table = arrow::Table::Make(combined_schema, columns);

        mlog(DEBUG, "Table was %ld rows and %d columns.", table->num_rows(), table->num_columns());
        mlog(DEBUG, "Table is  %ld rows and %d columns.", updated_table->num_rows(), updated_table->num_columns());

        WriteTableToParquetFile(updated_table, out_file);
    }
    catch (const RunTimeException &e)
    {
        mlog(e.level(), "Failed to read samples: %s", e.what());
    }

    /* Free samples */
    for(const RasterSample* sample : slist)
        delete sample;

    return 0;
}

/*----------------------------------------------------------------------------
 * Destructor
 *----------------------------------------------------------------------------*/
RasterObject::~RasterObject(void)
{
    /* Release GeoParms LuaObject */
    parms->releaseLuaObject();
}

/******************************************************************************
 * PROTECTED METHODS
 ******************************************************************************/

/*----------------------------------------------------------------------------
 * Constructor
 *----------------------------------------------------------------------------*/
RasterObject::RasterObject(lua_State *L, GeoParms* _parms):
    LuaObject(L, OBJECT_TYPE, LUA_META_NAME, LUA_META_TABLE),
    parms(_parms)
{
    /* Add Lua Functions */
    LuaEngine::setAttrFunc(L, "sample", luaSamples);
    LuaEngine::setAttrFunc(L, "parquet_sample", luaParquetSamples);
    LuaEngine::setAttrFunc(L, "subset", luaSubset);
    LuaEngine::setAttrFunc(L, "pixels", luaPixels);
}

/*----------------------------------------------------------------------------
 * fileDictAdd
 *----------------------------------------------------------------------------*/
uint64_t RasterObject::fileDictAdd(const std::string& fileName)
{
    uint64_t id;

    if(!fileDict.find(fileName.c_str(), &id))
    {
        id = (parms->key_space << 32) | fileDict.length();
        fileDict.add(fileName.c_str(), id);
    }

    return id;
}

/*----------------------------------------------------------------------------
 * luaSamples - :sample(lon, lat, [height], [gps]) --> in|out
 *----------------------------------------------------------------------------*/
int RasterObject::luaSamples(lua_State *L)
{
    uint32_t err = SS_NO_ERRORS;
    int num_ret = 1;

    RasterObject *lua_obj = NULL;
    std::vector<RasterSample*> slist;

    try
    {
        /* Get Self */
        lua_obj = dynamic_cast<RasterObject*>(getLuaSelf(L, 1));

        /* Get Coordinates */
        double lon    = getLuaFloat(L, 2);
        double lat    = getLuaFloat(L, 3);
        double height = getLuaFloat(L, 4, true, 0.0);
        const char* closest_time_str = getLuaString(L, 5, true, NULL);

        /* Get gps closest time (overrides params provided closest time) */
        int64_t gps = 0;
        if(closest_time_str != NULL)
        {
            gps = TimeLib::str2gpstime(closest_time_str);
        }

        /* Get samples */
        bool listvalid = true;
        OGRPoint poi(lon, lat, height);
        err = lua_obj->getSamples(&poi, gps, slist, NULL);

        if(err & SS_THREADS_LIMIT_ERROR)
        {
            listvalid = false;
            mlog(CRITICAL, "Too many rasters to sample, max allowed: %d, limit your AOI/temporal range or use filters", GeoIndexedRaster::MAX_READER_THREADS);
        }

        /* Create return table */
        lua_createtable(L, slist.size(), 0);
        num_ret++;

        /* Populate samples */
        if(listvalid && !slist.empty())
        {
            for(uint32_t i = 0; i < slist.size(); i++)
            {
                const RasterSample* sample = slist[i];
                const char* fileName = "";

                /* Find fileName from fileId */
                Dictionary<uint64_t>::Iterator iterator(lua_obj->fileDictGet());
                for(int j = 0; j < iterator.length; j++)
                {
                    if(iterator[j].value == sample->fileId)
                    {
                        fileName = iterator[j].key;
                        break;
                    }
                }

                lua_createtable(L, 0, 4);
                LuaEngine::setAttrStr(L, "file", fileName);

                if(lua_obj->parms->zonal_stats) /* Include all zonal stats */
                {
                    LuaEngine::setAttrNum(L, "mad", sample->stats.mad);
                    LuaEngine::setAttrNum(L, "stdev", sample->stats.stdev);
                    LuaEngine::setAttrNum(L, "median", sample->stats.median);
                    LuaEngine::setAttrNum(L, "mean", sample->stats.mean);
                    LuaEngine::setAttrNum(L, "max", sample->stats.max);
                    LuaEngine::setAttrNum(L, "min", sample->stats.min);
                    LuaEngine::setAttrNum(L, "count", sample->stats.count);
                }

                if(lua_obj->parms->flags_file) /* Include flags */
                {
                    LuaEngine::setAttrNum(L, "flags", sample->flags);
                }

                LuaEngine::setAttrInt(L, "fileid", sample->fileId);
                LuaEngine::setAttrNum(L, "time", sample->time);
                LuaEngine::setAttrNum(L, "value", sample->value);
                lua_rawseti(L, -2, i+1);
            }
        } else mlog(DEBUG, "No samples read for (%.2lf, %.2lf)", lon, lat);
    }
    catch (const RunTimeException &e)
    {
        mlog(e.level(), "Failed to read samples: %s", e.what());
    }

    /* Free samples */
    for (const RasterSample* sample : slist)
        delete sample;

    /* Return Errors and Table of Samples */
    lua_pushinteger(L, err);
    return num_ret;
}








/*----------------------------------------------------------------------------
 * luaParquetSamples - :parquet_sample(in_file, out_file, [gps]) --> in|out
 *----------------------------------------------------------------------------*/
int RasterObject::luaParquetSamples(lua_State *L)
{
    int num_ret = 1;

    try
    {
        RasterObject* lua_obj = NULL;

        /* Get Self */
        lua_obj = dynamic_cast<RasterObject*>(getLuaSelf(L, 1));

        /* Get Coordinates */
        const char* in_file    = getLuaString(L, 2);
        const char* out_file   = getLuaString(L, 3);
        // double height          = getLuaFloat(L, 4, true, 0.0);
        const char* closest_time_str = getLuaString(L, 5, true, NULL);

        /* Get gps closest time (overrides params provided closest time) */
        int64_t gps = 0;
        if(closest_time_str != NULL)
        {
            gps = TimeLib::str2gpstime(closest_time_str);
        }

        std::string in_file_str(in_file);
        std::string out_file_str(out_file);
        std::string prefix = "eric";

        lua_obj->getSamples(in_file_str, out_file_str, prefix, gps);
    }
    catch (const RunTimeException &e)
    {
        mlog(e.level(), "Failed to read samples: %s", e.what());
    }

    lua_pushinteger(L, 0);
    return num_ret;
}


/*----------------------------------------------------------------------------
 * luaSubset - :subset(lon_min, lat_min, lon_max, lat_max) --> in|out
 *----------------------------------------------------------------------------*/
int RasterObject::luaSubset(lua_State *L)
{
    uint32_t err = SS_NO_ERRORS;
    int num_ret = 1;

    RasterObject *lua_obj = NULL;
    std::vector<RasterSubset*> slist;

    try
    {
        /* Get Self */
        lua_obj = dynamic_cast<RasterObject*>(getLuaSelf(L, 1));

        /* Get extent */
        double lon_min = getLuaFloat(L, 2);
        double lat_min = getLuaFloat(L, 3);
        double lon_max = getLuaFloat(L, 4);
        double lat_max = getLuaFloat(L, 5);
        const char* closest_time_str = getLuaString(L, 6, true, NULL);

        /* Get gps closest time (overrides params provided closest time) */
        int64_t gps = 0;
        if(closest_time_str != NULL)
        {
            gps = TimeLib::str2gpstime(closest_time_str);
        }

        /* Get subset */
        OGRPolygon poly = GdalRaster::makeRectangle(lon_min, lat_min, lon_max, lat_max);
        err = lua_obj->getSubsets(&poly, gps, slist, NULL);
        num_ret += lua_obj->slist2table(slist, err, L);
    }
    catch (const RunTimeException &e)
    {
        mlog(e.level(), "Failed to subset raster: %s", e.what());
    }

    /* Free subsets */
    for (const RasterSubset* subset : slist)
        delete subset;

    /* Return Errors and Table of Samples */
    lua_pushinteger(L, err);

    return num_ret;
}


/*----------------------------------------------------------------------------
 * luaPixels - :pixels(ulx, uly, xsize, ysize) --> in|out
 *----------------------------------------------------------------------------*/
int RasterObject::luaPixels(lua_State *L)
{
    uint32_t err = SS_NO_ERRORS;
    int num_ret = 1;

    RasterObject *lua_obj = NULL;
    std::vector<RasterSubset*> slist;

    try
    {
        /* Get Self */
        lua_obj = dynamic_cast<RasterObject*>(getLuaSelf(L, 1));

        /* Get extent */
        uint32_t ulx = getLuaInteger(L, 2);
        uint32_t uly = getLuaInteger(L, 3);
        uint32_t xsize = getLuaInteger(L, 4);
        uint32_t ysize = getLuaInteger(L, 5);

        /* Get pixels  */
        err = lua_obj->getPixels(ulx, uly, xsize, ysize, slist, NULL);
        num_ret += lua_obj->slist2table(slist, err, L);
    }
    catch (const RunTimeException &e)
    {
        mlog(e.level(), "Failed to subset raster: %s", e.what());
    }

    /* Free subsets */
    for (const RasterSubset* subset : slist)
        delete subset;

    /* Return Errors and Table of Samples */
    lua_pushinteger(L, err);
    return num_ret;
}


/******************************************************************************
 * PRIVATE METHODS
 ******************************************************************************/

/*----------------------------------------------------------------------------
 * slist2table
 *----------------------------------------------------------------------------*/
int RasterObject::slist2table(const std::vector<RasterSubset*>& slist, uint32_t errors, lua_State *L)
{
    RasterObject* lua_obj = dynamic_cast<RasterObject*>(getLuaSelf(L, 1));
    int num_ret = 0;

    bool listvalid = true;
    if(errors & SS_THREADS_LIMIT_ERROR)
    {
        listvalid = false;
        mlog(CRITICAL, "Too many rasters to subset, max allowed: %d, limit your AOI/temporal range or use filters", GeoIndexedRaster::MAX_READER_THREADS);
    }

    if(errors & SS_MEMPOOL_ERROR)
    {
        listvalid = false;
        mlog(CRITICAL, "Some rasters could not be subset, requested memory size > max allowed: %ld MB", RasterSubset::MAX_SIZE / (1024 * 1024));
    }

    /* Create return table */
    lua_createtable(L, slist.size(), 0);
    num_ret++;

    /* Populate subsets */
    if(listvalid && !slist.empty())
    {
        for(uint32_t i = 0; i < slist.size(); i++)
        {
            const RasterSubset* subset = slist[i];
            const char* fileName = "";

            /* Find fileName from fileId */
            Dictionary<uint64_t>::Iterator iterator(lua_obj->fileDictGet());
            for(int j = 0; j < iterator.length; j++)
            {
                if(iterator[j].value == subset->fileId)
                {
                    fileName = iterator[j].key;
                    break;
                }
            }

            /* Populate Return Results */
            lua_createtable(L, 0, 8);
            LuaEngine::setAttrStr(L, "file", fileName);
            LuaEngine::setAttrInt(L, "fileid", subset->fileId);
            LuaEngine::setAttrNum(L, "time", subset->time);
            if(subset->size < 0x1000000) // 16MB
            {
                std::string data_b64_str = MathLib::b64encode(subset->data, subset->size);
                LuaEngine::setAttrStr(L, "data", data_b64_str.c_str(), data_b64_str.size());
            }
            else
            {
                LuaEngine::setAttrStr(L, "data", "", 0);
            }
            LuaEngine::setAttrInt(L, "cols", subset->cols);
            LuaEngine::setAttrInt(L, "rows", subset->rows);
            LuaEngine::setAttrInt(L, "size", subset->size);
            LuaEngine::setAttrNum(L, "datatype", subset->datatype);
            LuaEngine::setAttrNum(L, "ulx", subset->map_ulx);
            LuaEngine::setAttrNum(L, "uly", subset->map_uly);
            LuaEngine::setAttrNum(L, "cellsize", subset->cellsize);
            LuaEngine::setAttrNum(L, "bbox.lonmin", subset->bbox.lon_min);
            LuaEngine::setAttrNum(L, "bbox.latmin", subset->bbox.lat_min);
            LuaEngine::setAttrNum(L, "bbox.lonmax", subset->bbox.lon_max);
            LuaEngine::setAttrNum(L, "bbox.latmax", subset->bbox.lat_max);
            LuaEngine::setAttrStr(L, "wkt", subset->wkt.c_str());
            lua_rawseti(L, -2, i + 1);
        }
    }
    else mlog(DEBUG, "No subsets read");

    return num_ret;
}

