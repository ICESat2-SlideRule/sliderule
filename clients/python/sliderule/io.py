# Copyright (c) 2021, University of Washington
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# 3. Neither the name of the University of Washington nor the names of its
#    contributors may be used to endorse or promote products derived from this
#    software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY OF WASHINGTON AND CONTRIBUTORS
# “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE UNIVERSITY OF WASHINGTON OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import sys
import json
import warnings
import datetime
import numpy as np
from sliderule import logger, SLIDERULE_EPSG

# imports with warnings if not present
try:
    import scipy.io
except ModuleNotFoundError as e:
    sys.stderr.write("Warning: missing packages, some functions will throw an exception if called. (%s)\n" % (str(e)))
try:
    import geopandas
    from geopandas.io.arrow import _geopandas_to_arrow
    from geopandas._compat import import_optional_dependency
except ModuleNotFoundError as e:
    sys.stderr.write("Warning: missing packages, some functions will throw an exception if called. (%s)\n" % (str(e)))  
try:
    import h5py
except ModuleNotFoundError as e:
    sys.stderr.write("Warning: missing packages, some functions will throw an exception if called. (%s)\n" % (str(e)))

# attributes for ATL06-SR and ATL03 variables
def get_attributes(**kwargs):
    # set default keyword arguments
    kwargs.setdefault('lon_key','longitude')
    kwargs.setdefault('lat_key','latitude')
    lon_key,lat_key = (kwargs['lon_key'],kwargs['lat_key'])
    coordinates = f'{lat_key} {lon_key}'
    attrs = {}
    # file level attributes
    attrs['featureType'] = 'trajectory'
    attrs['title'] = "ATLAS/ICESat-2 SlideRule Height"
    attrs['reference'] = 'https://doi.org/10.5281/zenodo.5484048'
    attrs['date_created'] = datetime.datetime.now().isoformat()
    attrs['geospatial_lat_units'] = "degrees_north"
    attrs['geospatial_lon_units'] = "degrees_east"
    attrs['geospatial_ellipsoid'] = "WGS84"
    attrs['date_type'] = "UTC"
    attrs['time_type'] = "CCSDS UTC-A"
    # segment ID
    attrs['segment_id'] = {}
    attrs['segment_id']['long_name'] = "Along-track segment ID number"
    attrs['segment_id']['coordinates'] = coordinates
    # delta time
    attrs['delta_time'] = {}
    attrs['delta_time']['units'] = "seconds since 2018-01-01"
    attrs['delta_time']['long_name'] = "Elapsed GPS seconds"
    attrs['delta_time']['standard_name'] = "time"
    attrs['delta_time']['calendar'] = "standard"
    attrs['delta_time']['coordinates'] = coordinates
    # latitude
    attrs[lat_key] = {}
    attrs[lat_key]['units'] = "degrees_north"
    attrs[lat_key]['long_name'] = "Latitude"
    attrs[lat_key]['standard_name'] = "latitude"
    attrs[lat_key]['valid_min'] = -90.0
    attrs[lat_key]['valid_max'] = 90.0
    # longitude
    attrs[lon_key] = {}
    attrs[lon_key]['units'] = "degrees_east"
    attrs[lon_key]['long_name'] = "Longitude"
    attrs[lon_key]['standard_name'] = "longitude"
    attrs[lon_key]['valid_min'] = -180.0
    attrs[lon_key]['valid_max'] = 180.0
    # mean height from fit
    attrs['h_mean'] = {}
    attrs['h_mean']['units'] = "meters"
    attrs['h_mean']['long_name'] = "Height Mean"
    attrs['h_mean']['coordinates'] = coordinates
    # uncertainty in mean height
    attrs['h_sigma'] = {}
    attrs['h_sigma']['units'] = "meters"
    attrs['h_sigma']['long_name'] = "Height Error"
    attrs['h_sigma']['coordinates'] = coordinates
    # RMS of fit
    attrs['rms_misfit'] = {}
    attrs['rms_misfit']['units'] = "meters"
    attrs['rms_misfit']['long_name'] = "RMS of fit"
    attrs['rms_misfit']['coordinates'] = coordinates
    # along track slope
    attrs['dh_fit_dx'] = {}
    attrs['dh_fit_dx']['units'] = "meters/meters"
    attrs['dh_fit_dx']['long_name'] = "Along Track Slope"
    attrs['dh_fit_dx']['coordinates'] = coordinates
    # across track slope
    attrs['dh_fit_dy'] = {}
    attrs['dh_fit_dy']['units'] = "meters/meters"
    attrs['dh_fit_dy']['long_name'] = "Across Track Slope"
    attrs['dh_fit_dy']['coordinates'] = coordinates
    # number of photons in fit
    attrs['n_fit_photons'] = {}
    attrs['n_fit_photons']['units'] = "1"
    attrs['n_fit_photons']['long_name'] = "Number of Photons in Fit"
    attrs['n_fit_photons']['coordinates'] = coordinates
    # surface fit window
    attrs['w_surface_window_final'] = {}
    attrs['w_surface_window_final']['units'] = "meters"
    attrs['w_surface_window_final']['long_name'] = "Surface Window Width"
    attrs['w_surface_window_final']['coordinates'] = coordinates
    # robust dispersion estimate of fit
    attrs['h_robust_sprd'] = {}
    attrs['h_robust_sprd']['units'] = "meters"
    attrs['h_robust_sprd']['long_name'] = "Robust Spread"
    attrs['h_robust_sprd']['coordinates'] = coordinates
    # orbital cycle
    attrs['cycle'] = {}
    attrs['cycle']['long_name'] = "Orbital cycle"
    attrs['cycle']['coordinates'] = coordinates
    # RGT
    attrs['rgt'] = {}
    attrs['rgt']['long_name'] = "Reference Ground Track"
    attrs['rgt']['valid_min'] = 1
    attrs['rgt']['valid_max'] = 1387
    attrs['rgt']['coordinates'] = coordinates
    # ground track
    attrs['gt'] = {}
    attrs['gt']['long_name'] = "Ground track identifier"
    attrs['gt']['flag_values'] = [10, 20, 30, 40, 50, 60]
    attrs['gt']['flag_meanings'] = "GT1L, GT1R, GT2L, GT2R, GT3L, GT3R"
    attrs['gt']['valid_min'] = 10
    attrs['gt']['valid_max'] = 60
    attrs['gt']['coordinates'] = coordinates
    # along-track distance
    attrs['x_atc'] = {}
    attrs['x_atc']['units'] = "meters"
    attrs['x_atc']['long_name'] = "Along track distance from equator"
    attrs['x_atc']['coordinates'] = coordinates
    # across-track distance
    attrs['y_atc'] = {}
    attrs['y_atc']['units'] = "meters"
    attrs['y_atc']['long_name'] = "Across track distance from reference ground track"
    attrs['y_atc']['coordinates'] = coordinates
    # spot
    attrs['spot'] = {}
    attrs['spot']['long_name'] = "ATLAS spot number"
    attrs['spot']['valid_min'] = 1
    attrs['spot']['valid_max'] = 6
    attrs['spot']['coordinates'] = coordinates
    # pflags
    attrs['pflags'] = {}
    attrs['pflags']['long_name'] = "Processing Flags"
    attrs['pflags']['flag_values'] = [0, 1, 2, 4]
    attrs['pflags']['flag_meanings'] = ("valid, spread too short, "
        "too few photons, max iterations reached")
    attrs['pflags']['valid_min'] = 0
    attrs['pflags']['valid_max'] = 4
    attrs['pflags']['coordinates'] = coordinates
    # ATL03 specific variables
    # segment_dist
    attrs['segment_dist'] = {}
    attrs['segment_dist']['units'] = 'meters'
    attrs['segment_dist']['long_name'] = ("Along track distance "
        "from equator for segment")
    attrs['segment_dist']['coordinates'] = coordinates
    # sc_orient
    attrs['sc_orient'] = {}
    attrs['sc_orient']['long_name'] = "Spacecraft Orientation"
    attrs['sc_orient']['flag_values'] = [0, 1, 2]
    attrs['sc_orient']['flag_meanings'] = "backward forward transition"
    attrs['sc_orient']['valid_min'] = 0
    attrs['sc_orient']['valid_max'] = 2
    attrs['sc_orient']['coordinates'] = coordinates
    # track
    attrs['track'] = {}
    attrs['track']['long_name'] = "Pair track identifier"
    attrs['track']['flag_values'] = [1, 2, 3]
    attrs['track']['flag_meanings'] = "PT1, PT2, PT3"
    attrs['track']['valid_min'] = 1
    attrs['track']['valid_max'] = 3
    attrs['track']['coordinates'] = coordinates
    # pair
    attrs['pair'] = {}
    attrs['pair']['long_name'] = "left-right identifier of pair track"
    attrs['pair']['flag_values'] = [0, 1]
    attrs['pair']['flag_meanings'] = "left, right"
    attrs['pair']['valid_min'] = 1
    attrs['pair']['valid_max'] = 3
    attrs['pair']['coordinates'] = coordinates
    # photon height
    attrs['height'] = {}
    attrs['height']['units'] = "meters"
    attrs['height']['long_name'] = "Photon Height"
    attrs['height']['coordinates'] = coordinates
    # photon height
    attrs['height'] = {}
    attrs['height']['units'] = "meters"
    attrs['height']['long_name'] = "Photon Height"
    attrs['height']['coordinates'] = coordinates
    # quality_ph
    attrs['quality_ph'] = {}
    attrs['quality_ph']['long_name'] = "Photon Quality"
    attrs['quality_ph']['flag_values'] = [0, 1, 2, 3]
    attrs['quality_ph']['flag_meanings'] = ("nominal possible_afterpulse "
        "possible_impulse_response_effect possible_tep")
    attrs['quality_ph']['valid_min'] = 1
    attrs['quality_ph']['valid_max'] = 3
    attrs['quality_ph']['coordinates'] = coordinates
    # atl03_cnf
    attrs['atl03_cnf'] = {}
    attrs['atl03_cnf']['long_name'] = "Photon Signal Confidence"
    attrs['atl03_cnf']['flag_values'] = [-2, 1, 0, 1, 2, 3, 4]
    attrs['atl03_cnf']['flag_meanings'] = ("possible_tep "
        "not_considered noise buffer low medium high")
    attrs['atl03_cnf']['valid_min'] = -2
    attrs['atl03_cnf']['valid_max'] = 3
    attrs['atl03_cnf']['coordinates'] = coordinates
    # atl08_class
    attrs['atl08_class'] = {}
    attrs['atl08_class']['long_name'] = "ATL08 Photon Classification"
    attrs['atl08_class']['flag_values'] = [0, 1, 2, 3, 4]
    attrs['atl08_class']['flag_meanings'] = ("noise "
        "ground canopy top_of_canopy unclassified")
    attrs['atl08_class']['valid_min'] = 0
    attrs['atl08_class']['valid_max'] = 4
    attrs['atl08_class']['coordinates'] = coordinates
    # yapc_score
    attrs['yapc_score'] = {}
    attrs['yapc_score']['units'] = "1"
    attrs['yapc_score']['long_name'] = "YAPC Photon Weight"
    attrs['yapc_score']['valid_min'] = 0
    attrs['yapc_score']['valid_max'] = 255
    attrs['yapc_score']['coordinates'] = coordinates
    # return the attributes for the sliderule variables
    return attrs

# PURPOSE: encoder for creating the file attributes
def attributes_encoder(attr):
    """Custom encoder for creating file attributes in Python 3"""
    if isinstance(attr, (bytes, bytearray)):
        return attr.decode('utf-8')
    if isinstance(attr, (np.int_, np.intc, np.intp, np.int8, np.int16, np.int32,
        np.int64, np.uint8, np.uint16, np.uint32, np.uint64)):
        return int(attr)
    elif isinstance(attr, (np.float16, np.float32, np.float64)):
        return float(attr)
    elif isinstance(attr, (np.ndarray)):
        return attr.tolist()
    elif isinstance(attr, (np.bool_)):
        return bool(attr)
    elif isinstance(attr, (np.void)):
        return None
    else:
        return attr

# conversion factors between time units and seconds
_to_sec = {'microseconds': 1e-6, 'microsecond': 1e-6,
           'microsec': 1e-6, 'microsecs': 1e-6,
           'milliseconds': 1e-3, 'millisecond': 1e-3,
           'millisec': 1e-3, 'millisecs': 1e-3,
           'msec': 1e-3, 'msecs': 1e-3, 'ms': 1e-3,
           'seconds': 1.0, 'second': 1.0, 'sec': 1.0,
           'secs': 1.0, 's': 1.0,
           'minutes': 60.0, 'minute': 60.0,
           'min': 60.0, 'mins': 60.0,
           'hours': 3600.0, 'hour': 3600.0,
           'hr': 3600.0, 'hrs': 3600.0, 'h': 3600.0,
           'day': 86400.0, 'days': 86400.0, 'd': 86400.0}

# PURPOSE: split a date string into units and epoch
def split_date_string(date_string: str):
    """
    split a date string into units and epoch

    Parameters
    ----------
    date_string: str
        time-units since yyyy-mm-dd hh:mm:ss
    """
    try:
        units,_,epoch = date_string.split(None, 2)
    except ValueError:
        raise ValueError(f'Invalid format: {date_string}')
    else:
        return (units.lower(), epoch)

# PURPOSE: convert a numpy datetime array to delta times from the UNIX epoch
def convert_datetime(date, epoch='2018-01-01T00:00:00'):
    """
    Convert a numpy datetime array to seconds since ``epoch``

    Parameters
    ----------
    date: obj
        numpy datetime array
    epoch: str, default '2018-01-01T00:00:00'
        epoch for output delta_time

    Returns
    -------
    delta_time: float
        seconds since epoch
    """
    return (date - np.datetime64(epoch)) / np.timedelta64(1, 's')

# calculate centroid of polygon
def centroid(x,y):
    npts = len(x)
    area,cx,cy = (0.0,0.0,0.0)
    for i in range(npts-1):
        SA = x[i]*y[i+1] - x[i+1]*y[i]
        area += SA
        cx += (x[i] + x[i+1])*SA
        cy += (y[i] + y[i+1])*SA
    cx /= 3.0*area
    cy /= 3.0*area
    return (cx,cy)

# determine if polygon winding is counter-clockwise
def winding(x,y):
    npts = len(x)
    wind = np.sum([(x[i+1] - x[i])*(y[i+1] + y[i]) for i in range(npts - 1)])
    return wind

# geodesic area of a polygon in square kilometers
def area(lon, lat, a=6378.1370):
    assert len(lon) == len(lat), "Longitude and Latitude arrays must be the same length"
    assert len(lon) > 2, "Need at least 3 points to calculate area"
    # factor for converting to radians
    dtr = np.pi/180.0
    # initialize area
    area = 0.0
    npts = len(lon)
    # for each point in the polygon
    for i in range(npts):
        # wrap coordinates
        ii = (i + 1) % npts
        # calculate geodesic area in square kilometers
        area += (a**2)*(lon[ii]*dtr - lon[i]*dtr) * \
            (2.0 + np.sin(lat[i]*dtr) + np.sin(lat[ii]*dtr))/2.0
    # return absolute value of area
    return np.abs(area).squeeze()

# fix longitudes to be -180:180
def wrap_longitudes(lon):
    phi = np.arctan2(np.sin(lon*np.pi/180.0),np.cos(lon*np.pi/180.0))
    # convert phi from radians to degrees
    return phi*180.0/np.pi

# convert coordinates to a sliderule region
def to_region(lon,lat):
    region = [{'lon':ln,'lat':lt} for ln,lt in np.c_[lon,lat]]
    return region

# extract coordinates from a sliderule region
def from_region(polygon):
    npts = len(polygon)
    x = np.zeros((npts))
    y = np.zeros((npts))
    for i,p in enumerate(polygon):
        x[i] = p['lon']
        y[i] = p['lat']
    return (x,y)

# convert geodataframe vector object to a list of sliderule regions
def from_geodataframe(gdf):
    # verify that geodataframe is in latitude/longitude
    geodataframe = gdf.to_crs(SLIDERULE_EPSG)
    # create a list of regions
    regions = []
    # for each region
    for geometry in geodataframe.geometry:
        regions.append([{'lon':ln,'lat':lt} for ln,lt in geometry.exterior.coords])
    # return the list of regions
    return regions

# output request parameters to JSON
def to_json(filename, **kwargs):
    # set default keyword arguments
    kwargs.setdefault('parameters',None)
    kwargs.setdefault('regions',[])
    kwargs.setdefault('crs',SLIDERULE_EPSG)
    # import optional dependencies
    pyproj = import_optional_dependency(
        "pyproj", extra="pyproj is required for CRS conversion."
    )
    # add each parameter as an attribute
    SRparams = ['H_min_win', 'atl08_class', 'atl03_quality', 'ats', 'cnf',
        'cnt', 'len', 'maxi', 'res', 'sigma_r_max', 'srt', 'yapc']
    output = {}
    # for each adjustable sliderule parameter
    for p in SRparams:
        # try to convert the parameter if available
        try:
            output[p] = attributes_encoder(kwargs['parameters'][p])
        except:
            pass
    # save CRS to JSON
    crs = pyproj.CRS.from_string(kwargs['crs'])
    output['crs'] = crs.to_string()
    # save each region following GeoJSON specification
    output['type'] = 'FeatureCollection'
    output['features'] = []
    for i,poly in enumerate(kwargs['regions']):
        lon, lat = from_region(poly)
        lon = attributes_encoder(lon)
        lat = attributes_encoder(lat)
        geometry=dict(type="polygon", coordinates=[])
        geometry['coordinates'].append([[ln,lt] for ln,lt in zip(lon,lat)])
        output['features'].append(dict(type="Feature", geometry=geometry))
    # dump the attributes to a JSON file
    with open(filename, 'w') as fid:
        json.dump(output, fid)
    # print the filename and dictionary structure
    logger.info(filename)
    logger.info(list(output.keys()))

# read request parameters and regions from JSON
def from_json(filename, **kwargs):
    # load the JSON file
    with open(filename, 'r') as fid:
        attributes = json.load(fid)
    # print the filename and dictionary structure
    logger.info(filename)
    logger.info(list(attributes.keys()))
    # try to get the sliderule adjustable parameters
    SRparams = ['H_min_win', 'atl08_class', 'atl03_quality', 'ats', 'cnf',
        'cnt', 'len', 'maxi', 'res', 'sigma_r_max', 'srt', 'yapc']
    parms = {}
    # for each adjustable sliderule parameter
    for p in SRparams:
        # read the parameter if available
        try:
            parms[p] = attributes[p]
        except:
            pass
    # create a list of regions
    regions = []
    # for each feature in the JSON file
    for feature in attributes['features']:
        # for each coordinate set in the feature
        for coords in feature['geometry']['coordinates']:
            # append to sliderule regions
            regions.append([{'lon':ln,'lat':lt} for ln,lt in coords])
    # return the sliderule parameters and regions
    return (parms, regions)

def to_parquet(gdf, filename, **kwargs):
    # set default keyword arguments
    kwargs.setdefault('index',None)
    kwargs.setdefault('compression','snappy')
    kwargs.setdefault('schema_version',None)
    kwargs.setdefault('parameters',dict())
    kwargs.setdefault('regions',[])
    kwargs.setdefault('crs',SLIDERULE_EPSG)
    # import optional dependencies
    pyproj = import_optional_dependency(
        "pyproj", extra="pyproj is required for CRS conversion."
    )
    parquet = import_optional_dependency(
        "pyarrow.parquet", extra="pyarrow is required for Parquet support."
    )
    # output metadata
    output = {}
    # remove request polygon from output parameters
    if 'poly' in kwargs['parameters']:
        kwargs['parameters'].pop('poly')
    # for each adjustable sliderule parameter
    for p,val in kwargs['parameters'].items():
        # try to convert the parameter if available
        try:
            output[p] = attributes_encoder(val)
        except:
            pass
    # save CRS to JSON
    crs = pyproj.CRS.from_string(kwargs['crs'])
    output['crs'] = crs.to_json_dict()
    # save each region following GeoJSON specification
    output['type'] = 'FeatureCollection'
    output['features'] = []
    for i,poly in enumerate(kwargs['regions']):
        lon, lat = from_region(poly)
        lon = attributes_encoder(lon)
        lat = attributes_encoder(lat)
        geometry=dict(type="polygon", coordinates=[])
        geometry['coordinates'].append([[ln,lt] for ln,lt in zip(lon,lat)])
        output['features'].append(dict(type="Feature", geometry=geometry))
    # dump the attributes to encoded JSON-format
    sliderule_metadata = json.dumps(output).encode('utf-8') 
    # convert geodataframe to arrow table
    table = _geopandas_to_arrow(gdf,
        index=kwargs['index'],
        schema_version=kwargs['schema_version']
    )
    # store sliderule specific file-level metadata
    metadata = table.schema.metadata
    metadata.update({b"sliderule": sliderule_metadata})
    # replace schema metadata with updated
    table = table.replace_schema_metadata(metadata)
    # write arrow table to parquet file
    parquet.write_table(table, filename,
        compression=kwargs['compression']
    )

def from_parquet(filename, **kwargs):
    # set default keyword arguments
    kwargs.setdefault('crs',SLIDERULE_EPSG)
    kwargs.setdefault('return_parameters',False)
    kwargs.setdefault('return_regions',False)
    # import optional dependencies
    parquet = import_optional_dependency(
        "pyarrow.parquet", extra="pyarrow is required for Parquet support."
    )
    # read arrow table from parquet file
    # and convert to coordinate reference system
    gdf = geopandas.read_parquet(filename).to_crs(kwargs['crs'])
    # if not returning the query parameters or polygon
    if not (kwargs['return_parameters'] or kwargs['return_regions']):
        # return geodataframe
        return gdf
    # create tuple with returns
    output = (gdf,)
    # get parquet file metadata
    metadata = parquet.read_metadata(filename).metadata
    # validate sliderule metadata
    if b'sliderule' not in metadata.keys():
        logger.error("No sliderule metadata found in Parquet file")
        return output
    # decode sliderule metadata from JSON
    parms = json.loads(metadata[b'sliderule'].decode('utf-8'))
    # create a list of regions
    regions = []
    # for each feature in the JSON file
    for feature in parms.pop('features'):
        # for each coordinate set in the feature
        for coords in feature['geometry']['coordinates']:
            # append to sliderule regions
            regions.append([{'lon':ln,'lat':lt} for ln,lt in coords])
    # if returning the parameters
    if kwargs['return_parameters']:
        # add parameters to output tuple
        [parms.pop(v) for v in ('version','commit','type')]
        output += (parms,)
    # if returning the regions
    if kwargs['return_regions']:
        # add regions to output tuple
        output += (regions,)
    # return the combined tuple
    return output

# output geodataframe to netCDF (version 3)
def to_nc(gdf, filename, **kwargs):
    # add warning that function is deprecated
    logger.critical(f"Deprecated. Will be removed in a future release")
    # set default keyword arguments
    kwargs.setdefault('parameters',None)
    kwargs.setdefault('regions',[])
    kwargs.setdefault('crs',SLIDERULE_EPSG)
    kwargs.setdefault('lon_key','longitude')
    kwargs.setdefault('lat_key','latitude')
    kwargs.setdefault('units','seconds since 2018-01-01T00:00:00')
    # get output attributes
    attributes = get_attributes()
    # open netCDF3 file object (64-bit offset format)
    fileID = scipy.io.netcdf.netcdf_file(filename, 'w', version=2)
    warnings.filterwarnings("ignore")
    # convert geodataframe to pandas dataframe
    df = geopandas.pd.DataFrame(gdf.drop(columns='geometry'))
    # append latitude and longitude as columns
    lon_key,lat_key = (kwargs['lon_key'],kwargs['lat_key'])
    df[lat_key] = gdf['geometry'].values.y
    df[lon_key] = gdf['geometry'].values.x
    # get geodataframe coordinate system
    if gdf.crs:
        kwargs['crs'] = gdf.crs
    # create output delta time dimension and variable
    fileID.createDimension('delta_time', len(df.index))
    nc = fileID.createVariable('delta_time', 'f8', ('delta_time',))
    units, epoch = split_date_string(kwargs['units'])
    delta_time = convert_datetime(df.index, epoch)/_to_sec[units]
    nc[:] = delta_time.copy()
    # set attributes for delta time
    attributes['delta_time']['units'] = kwargs['units']
    attributes['delta_time']['long_name'] = "Elapsed GPS time"
    for att_key,att_val in attributes['delta_time'].items():
        setattr(nc, att_key, att_val)
    # for each variable in the dataframe
    for key,val in df.items():
        if np.issubdtype(val, np.unsignedinteger):
            nc = fileID.createVariable(key, 'i4', ('delta_time',))
            nc[:] = val.astype(np.int32)
        else:
            nc = fileID.createVariable(key, val.dtype, ('delta_time',))
            nc[:] = val.copy()
        # set attributes for variable
        for att_key,att_val in attributes[key].items():
            setattr(nc,att_key,att_val)
    # add file attributes
    fileID.featureType = attributes['featureType']
    fileID.title = attributes['title']
    fileID.reference = attributes['reference']
    fileID.date_created = attributes['date_created']
    fileID.date_type = attributes['date_type']
    fileID.time_type = attributes['time_type']
    # save geodataframe coordinate system
    fileID.crs = kwargs['crs']
    # add geospatial attributes
    if kwargs['crs'] in ('EPSG:4326', SLIDERULE_EPSG):
        fileID.geospatial_lat_units = \
            attributes['geospatial_lat_units']
        fileID.geospatial_lon_units = \
            attributes['geospatial_lon_units']
        fileID.geospatial_ellipsoid = \
            attributes['geospatial_ellipsoid']
    # add each parameter as an attribute
    SRparams = ['H_min_win', 'atl08_class', 'atl03_quality', 'ats', 'cnf',
        'cnt', 'len', 'maxi', 'res', 'sigma_r_max', 'srt', 'yapc']
    # for each adjustable sliderule parameter
    for p in SRparams:
        # try to get the parameter if available
        try:
            attr = attributes_encoder(kwargs['parameters'][p])
            setattr(fileID, p, json.dumps(attr))
        except:
            # if empty or unavailable
            pass
    # for each version parameter
    for p in ['version', 'commit']:
        # try to get the parameter if available
        try:
            setattr(fileID, p, kwargs['parameters'][p])
        except:
            # if empty or unavailable
            pass
    # save each region as a list attribute
    for i,poly in enumerate(kwargs['regions']):
        lon, lat = from_region(poly)
        lon = attributes_encoder(lon)
        lat = attributes_encoder(lat)
        setattr(fileID, 'poly{0:d}_x'.format(i), json.dumps(lon))
        setattr(fileID, 'poly{0:d}_y'.format(i), json.dumps(lat))
    # Output netCDF structure information
    logger.info(filename)
    logger.info(list(fileID.variables.keys()))
    # Closing the netCDF file
    fileID.close()
    warnings.filterwarnings("default")

# input geodataframe from netCDF (version 3)
def from_nc(filename, **kwargs):
    # add warning that function is deprecated
    logger.critical(f"Deprecated. Will be removed in a future release")
    # set default crs
    kwargs.setdefault('crs',SLIDERULE_EPSG)
    kwargs.setdefault('lon_key','longitude')
    kwargs.setdefault('lat_key','latitude')
    kwargs.setdefault('index_key','time')
    kwargs.setdefault('return_parameters',False)
    kwargs.setdefault('return_regions',False)
    # open netCDF3 file object (64-bit offset format)
    fileID = scipy.io.netcdf.netcdf_file(filename, 'r', version=2)
    warnings.filterwarnings("ignore")
    # input dictionary for input variables
    nc = {}
    # get each variable from netCDF
    for key,val in fileID.variables.items():
        # swap byte order to little endian if big endian
        flattened = val[:].squeeze()
        if (flattened.dtype.byteorder == '>'):
            nc[key] = flattened.byteswap().newbyteorder()
        else:
            nc[key] = flattened.copy()
    # get geodataframe coordinate system
    if getattr(fileID, 'crs'):
        kwargs['crs'] = fileID.crs.decode('utf-8')
    # parameter attributes to read
    SRparams = ['H_min_win', 'atl08_class', 'atl03_quality', 'ats', 'cnf',
        'cnt', 'len', 'maxi', 'res', 'sigma_r_max', 'srt', 'yapc']
    # for each adjustable sliderule parameter
    parms = {}
    for p in SRparams:
        # try to get the parameter if available
        try:
            parms[p] = json.loads(getattr(fileID, p))
        except:
            # if empty or unavailable
            pass
    # read each region from list attribute
    regions = []
    # counter variable for reading polygon attributes
    i = 0
    while True:
        # attempt to get x and y coordinates for query polygon
        try:
            x = json.loads(getattr(fileID, f'poly{i:d}_x'))
            y = json.loads(getattr(fileID, f'poly{i:d}_y'))
        except:
            break
        else:
            # convert x and y coordinates into sliderule region
            regions.append(to_region(x, y))
            # add to polygon counter
            i += 1
    # Generate Time Column
    units, epoch = split_date_string(fileID.variables['delta_time'].units.decode('utf-8'))
    delta_time = (1e9*_to_sec[units]*nc['delta_time']).astype('timedelta64[ns]')
    nc['time'] = geopandas.pd.to_datetime(np.datetime64(epoch) + delta_time)
    # remove delta time from dictionary
    del nc['delta_time']
    # generate geometry column
    lon_key,lat_key = (kwargs['lon_key'],kwargs['lat_key'])
    geometry = geopandas.points_from_xy(nc[lon_key],nc[lat_key])
    # remove coordinates from dictionary
    del nc[lon_key]
    del nc[lat_key]
    # Closing the netCDF file
    fileID.close()
    warnings.filterwarnings("default")
    # create Pandas DataFrame object
    df = geopandas.pd.DataFrame(nc)
    # build GeoDataFrame
    gdf = geopandas.GeoDataFrame(df, geometry=geometry, crs=kwargs['crs'])
    # set index
    gdf.set_index(kwargs['index_key'], inplace=True)
    gdf.sort_index(inplace=True)
    # if not returning the query parameters or polygon
    if not (kwargs['return_parameters'] or kwargs['return_regions']):
        # return geodataframe
        return gdf
    # create tuple with returns
    output = (gdf,)
    # if returning the parameters
    if kwargs['return_parameters']:
        # add parameters to output tuple
        output += (parms,)
    # if returning the regions
    if kwargs['return_regions']:
        # add regions to output tuple
        output += (regions,)
    # return the combined tuple
    return output

# output geodataframe to HDF5
def to_hdf(gdf, filename, **kwargs):
    # set default keyword arguments
    kwargs.setdefault('driver','pytables')
    kwargs.setdefault('parameters',None)
    kwargs.setdefault('regions',[])
    kwargs.setdefault('crs',SLIDERULE_EPSG)
    kwargs.setdefault('lon_key','longitude')
    kwargs.setdefault('lat_key','latitude')
    kwargs.setdefault('units','seconds since 2018-01-01T00:00:00')
    # get output attributes
    attributes = get_attributes()
    # convert geodataframe to pandas dataframe
    df = geopandas.pd.DataFrame(gdf.drop(columns='geometry'))
    # append latitude and longitude as columns
    lon_key,lat_key = (kwargs['lon_key'],kwargs['lat_key'])
    df[lat_key] = gdf['geometry'].values.y
    df[lon_key] = gdf['geometry'].values.x
    # get geodataframe coordinate system
    if gdf.crs:
        kwargs['crs'] = str(gdf.crs)
    # output to HDF5 format
    if (kwargs['driver'].lower() == 'pytables'):
        kwargs.pop('driver')
        # write dataframe to pytables HDF5
        write_pytables(df, filename, attributes, **kwargs)
    elif (kwargs['driver'].lower() == 'h5py'):
        kwargs.pop('driver')
        # write dataframe to HDF5
        write_h5py(df, filename, attributes, **kwargs)

# write pandas dataframe to pytables HDF5
def write_pytables(df, filename, attributes, **kwargs):
    # add warning that function is deprecated
    logger.critical(f"Deprecated. Will be removed in a future release")
    # set default keyword arguments
    kwargs.setdefault('parameters',None)
    kwargs.setdefault('regions',[])
    kwargs.setdefault('crs',SLIDERULE_EPSG)
    # write data to a pytables HDF5 file
    df.to_hdf(filename, 'sliderule_segments', format="table", mode="w")
    # add file attributes
    fileID = geopandas.pd.HDFStore(filename, mode='a')
    fileID.root._v_attrs.TITLE = attributes['title']
    fileID.root._v_attrs.reference = attributes['reference']
    fileID.root._v_attrs.date_created = attributes['date_created']
    fileID.root._v_attrs.date_type = attributes['date_type']
    fileID.root._v_attrs.time_type = attributes['time_type']
    # set coordinate reference system as attribute
    fileID.root._v_attrs.crs = kwargs['crs']
    # add geospatial attributes
    if kwargs['crs'] in ('EPSG:4326', SLIDERULE_EPSG):
        fileID.root._v_attrs.geospatial_lat_units = \
            attributes['geospatial_lat_units']
        fileID.root._v_attrs.geospatial_lon_units = \
            attributes['geospatial_lon_units']
        fileID.root._v_attrs.geospatial_ellipsoid = \
            attributes['geospatial_ellipsoid']
    # add each parameter as an attribute
    SRparams = ['H_min_win', 'atl08_class', 'atl03_quality', 'ats', 'cnf',
        'cnt', 'len', 'maxi', 'res', 'sigma_r_max', 'srt', 'yapc']
    # for each adjustable sliderule parameter
    for p in SRparams:
        # try to get the parameter if available
        try:
            attr = attributes_encoder(kwargs['parameters'][p])
            setattr(fileID.root._v_attrs, p, json.dumps(attr))
        except:
            # if empty or unavailable
            pass
    # for each version parameter
    for p in ['version', 'commit']:
        # try to get the parameter if available
        try:
            setattr(fileID.root._v_attrs, p, kwargs['parameters'][p])
        except:
            # if empty or unavailable
            pass
    # save each region as a list attribute
    for i,poly in enumerate(kwargs['regions']):
        lon, lat = from_region(poly)
        lon = attributes_encoder(lon)
        lat = attributes_encoder(lat)
        setattr(fileID.root._v_attrs, f'poly{i:d}_x', json.dumps(lon))
        setattr(fileID.root._v_attrs, f'poly{i:d}_y', json.dumps(lat))
    # Output HDF5 structure information
    logger.info(filename)
    logger.info(fileID.get_storer('sliderule_segments').non_index_axes[0][1])
    # Closing the HDF5 file
    fileID.close()

# write pandas dataframe to h5py HDF5
def write_h5py(df, filename, attributes, **kwargs):
    # add warning that function is deprecated
    logger.critical(f"Deprecated. Will be removed in a future release")
    # set default keyword arguments
    kwargs.setdefault('parameters',None)
    kwargs.setdefault('regions',[])
    kwargs.setdefault('crs',SLIDERULE_EPSG)
    kwargs.setdefault('units','seconds since 2018-01-01T00:00:00')
    # open HDF5 file object
    fileID = h5py.File(filename, mode='w')
    # create HDF5 records
    h5 = {}
    # create output delta time variable
    key = 'delta_time'
    units, epoch = split_date_string(kwargs['units'])
    delta_time = convert_datetime(df.index, epoch)/_to_sec[units]
    h5[key] = fileID.create_dataset(key, delta_time.shape, data=delta_time,
        dtype=delta_time.dtype, compression='gzip')
    # set attributes for delta time
    attributes['delta_time']['units'] = kwargs['units']
    attributes['delta_time']['long_name'] = "Elapsed GPS time"
    for att_key,att_val in attributes[key].items():
        h5[key].attrs[att_key] = att_val
    # for each variable in the dataframe
    for key,val in df.items():
        # create dataset for variable
        h5[key] = fileID.create_dataset(key, val.shape, data=val,
            dtype=val.dtype, compression='gzip')
        h5[key].dims[0].attach_scale(h5['delta_time'])
        # set attributes for variable
        for att_key,att_val in attributes[key].items():
            h5[key].attrs[att_key] = att_val
    # add file attributes
    fileID.attrs['featureType'] = attributes['featureType']
    fileID.attrs['title'] = attributes['title']
    fileID.attrs['reference'] = attributes['reference']
    fileID.attrs['date_created'] = attributes['date_created']
    fileID.attrs['date_type'] = attributes['date_type']
    fileID.attrs['time_type'] = attributes['time_type']
    # set coordinate reference system as attribute
    fileID.attrs['crs'] = kwargs['crs']
    # add geospatial attributes
    if kwargs['crs'] in ('EPSG:4326', SLIDERULE_EPSG):
        fileID.attrs['geospatial_lat_units'] = \
            attributes['geospatial_lat_units']
        fileID.attrs['geospatial_lon_units'] = \
            attributes['geospatial_lon_units']
        fileID.attrs['geospatial_ellipsoid'] = \
            attributes['geospatial_ellipsoid']
    # add each parameter as an attribute
    SRparams = ['H_min_win', 'atl08_class', 'atl03_quality', 'ats', 'cnf',
        'cnt', 'len', 'maxi', 'res', 'sigma_r_max', 'srt', 'yapc']
    # for each adjustable sliderule parameter
    for p in SRparams:
        # try to get the parameter if available
        try:
            attr = attributes_encoder(kwargs['parameters'][p])
            fileID.attrs[p] = json.dumps(attr)
        except:
            # if empty or unavailable
            pass
    # for each version parameter
    for p in ['version', 'commit']:
        # try to get the parameter if available
        try:
            fileID.attrs[p] = kwargs['parameters'][p]
        except:
            # if empty or unavailable
            pass
    # save each region as a list attribute
    for i,poly in enumerate(kwargs['regions']):
        lon, lat = from_region(poly)
        lon = attributes_encoder(lon)
        lat = attributes_encoder(lat)
        fileID.attrs[f'poly{i:d}_x'] = json.dumps(lon)
        fileID.attrs[f'poly{i:d}_y'] = json.dumps(lat)
    # Output HDF5 structure information
    logger.info(filename)
    logger.info(list(fileID.keys()))
    # Closing the HDF5 file
    fileID.close()

# input geodataframe from HDF5
def from_hdf(filename, **kwargs):
    # set default keyword arguments
    kwargs.setdefault('driver','pytables')
    kwargs.setdefault('crs',SLIDERULE_EPSG)
    kwargs.setdefault('lon_key','longitude')
    kwargs.setdefault('lat_key','latitude')
    kwargs.setdefault('return_parameters',False)
    kwargs.setdefault('return_regions',False)
    if (kwargs['driver'].lower() == 'pytables'):
        kwargs.pop('driver')
        # return GeoDataFrame from pytables
        return read_pytables(filename, **kwargs)
    elif (kwargs['driver'].lower() == 'h5py'):
        kwargs.pop('driver')
        # return GeoDataFrame from h5py
        return read_h5py(filename, **kwargs)

# read pandas dataframe from pytables HDF5
def read_pytables(filename, **kwargs):
    # add warning that function is deprecated
    logger.critical(f"Deprecated. Will be removed in a future release")
    # set default crs
    kwargs.setdefault('crs',SLIDERULE_EPSG)
    kwargs.setdefault('lon_key','longitude')
    kwargs.setdefault('lat_key','latitude')
    kwargs.setdefault('return_parameters',False)
    kwargs.setdefault('return_regions',False)
    # open pytables HDF5 to read pandas dataframe
    df = geopandas.pd.read_hdf(filename, **kwargs)
    # generate geometry column
    lon_key,lat_key = (kwargs['lon_key'],kwargs['lat_key'])
    geometry = geopandas.points_from_xy(df[lon_key],df[lat_key])
    # get geodataframe coordinate system from attributes
    fileID = geopandas.pd.HDFStore(filename, mode='r')
    if getattr(fileID.root._v_attrs, 'crs'):
        kwargs['crs'] = str(fileID.root._v_attrs.crs)
    # parameter attributes to read
    SRparams = ['H_min_win', 'atl08_class', 'atl03_quality', 'ats', 'cnf',
        'cnt', 'len', 'maxi', 'res', 'sigma_r_max', 'srt', 'yapc']
    # for each adjustable sliderule parameter
    parms = {}
    for p in SRparams:
        # try to get the parameter if available
        try:
            parms[p] = json.loads(getattr(fileID.root._v_attrs,p))
        except:
            # if empty or unavailable
            pass
    # read each region from list attribute
    regions = []
    # counter variable for reading polygon attributes
    i = 0
    while True:
        # attempt to get x and y coordinates for query polygon
        try:
            x = json.loads(getattr(fileID.root._v_attrs,f'poly{i:d}_x'))
            y = json.loads(getattr(fileID.root._v_attrs,f'poly{i:d}_y'))
        except:
            break
        else:
            # convert x and y coordinates into sliderule region
            regions.append(to_region(x,y))
            # add to polygon counter
            i += 1
    # Closing the HDF5 file
    fileID.close()
    # build and return GeoDataFrame
    gdf = geopandas.GeoDataFrame(df.drop(columns=[lon_key,lat_key]),
        geometry=geometry, crs=kwargs['crs'])
    gdf.sort_index(inplace=True)
    # if not returning the query parameters or polygon
    if not (kwargs['return_parameters'] or kwargs['return_regions']):
        # return geodataframe
        return gdf
    # create tuple with returns
    output = (gdf,)
    # if returning the parameters
    if kwargs['return_parameters']:
        # add parameters to output tuple
        output += (parms,)
    # if returning the regions
    if kwargs['return_regions']:
        # add regions to output tuple
        output += (regions,)
    # return the combined tuple
    return output

# read pandas dataframe from h5py HDF5
def read_h5py(filename, **kwargs):
    # add warning that function is deprecated
    logger.critical(f"Deprecated. Will be removed in a future release")
    # set default crs
    kwargs.setdefault('crs',SLIDERULE_EPSG)
    kwargs.setdefault('lon_key','longitude')
    kwargs.setdefault('lat_key','latitude')
    kwargs.setdefault('index_key','time')
    kwargs.setdefault('return_parameters',False)
    kwargs.setdefault('return_regions',False)
    # open HDF5 file object
    fileID = h5py.File(filename, mode='r')
    # input dictionary for input variables
    h5 = {}
    # get each variable from HDF5
    for key,val in fileID.items():
        h5[key] = val[:].squeeze()
    # get geodataframe coordinate system from attributes
    if 'crs' in fileID.attrs.keys():
        kwargs['crs'] = str(fileID.attrs['crs'])
    # parameter attributes to read
    SRparams = ['H_min_win', 'atl08_class', 'atl03_quality', 'ats', 'cnf',
        'cnt', 'len', 'maxi', 'res', 'sigma_r_max', 'srt', 'yapc']
    # for each adjustable sliderule parameter
    parms = {}
    for p in SRparams:
        # try to get the parameter if available
        try:
            parms[p] = json.loads(fileID.attrs[p])
        except:
            # if empty or unavailable
            pass
    # read each region from list attribute
    regions = []
    # counter variable for reading polygon attributes
    i = 0
    while True:
        # attempt to get x and y coordinates for query polygon
        try:
            x = json.loads(fileID.attrs[f'poly{i:d}_x'])
            y = json.loads(fileID.attrs[f'poly{i:d}_y'])
        except:
            break
        else:
            # convert x and y coordinates into sliderule region
            regions.append(to_region(x,y))
            # add to polygon counter
            i += 1
    # Generate Time Column
    units, epoch = split_date_string(fileID['delta_time'].attrs['units'])
    delta_time = (1e9*_to_sec[units]*h5['delta_time']).astype('timedelta64[ns]')
    h5['time'] = geopandas.pd.to_datetime(np.datetime64(epoch) + delta_time)
    # remove delta time from dictionary
    del h5['delta_time']
    # generate geometry column
    lon_key,lat_key = (kwargs['lon_key'],kwargs['lat_key'])
    geometry = geopandas.points_from_xy(h5[lon_key],h5[lat_key])
    # remove coordinates from dictionary
    del h5[lon_key]
    del h5[lat_key]
    # Closing the HDF5 file
    fileID.close()
    # create Pandas DataFrame object
    df = geopandas.pd.DataFrame(h5)
    # build GeoDataFrame
    gdf = geopandas.GeoDataFrame(df, geometry=geometry, crs=kwargs['crs'])
    # set index
    gdf.set_index(kwargs['index_key'], inplace=True)
    gdf.sort_index(inplace=True)
    # if not returning the query parameters or polygon
    if not (kwargs['return_parameters'] or kwargs['return_regions']):
        # return geodataframe
        return gdf
    # create tuple with returns
    output = (gdf,)
    # if returning the parameters
    if kwargs['return_parameters']:
        # add parameters to output tuple
        output += (parms,)
    # if returning the regions
    if kwargs['return_regions']:
        # add regions to output tuple
        output += (regions,)
    # return the combined tuple
    return output

# output formats wrapper
def to_file(gdf, filename, format='parquet', **kwargs):
    if format.lower() in ('hdf','hdf5','h5'):
        to_hdf(gdf, filename, **kwargs)
    elif format.lower() in ('netcdf','nc'):
        to_nc(gdf, filename, **kwargs)
    elif format.lower() in ('geojson','csv','shp'):
        gdf.to_file(filename, **kwargs)
    elif format.lower() in ('geoparquet','parquet'):
        to_parquet(gdf, filename, **kwargs)

# input formats wrapper
def from_file(filename, format='parquet', **kwargs):
    if format.lower() in ('hdf','hdf5','h5'):
        return from_hdf(filename, **kwargs)
    elif format.lower() in ('netcdf','netcdf4','nc'):
        return from_nc(filename, **kwargs)
    elif format.lower() in ('geojson','csv','shp'):
        return geopandas.from_file(filename, **kwargs)
    elif format.lower() in ('geoparquet','parquet'):
        return from_parquet(filename, **kwargs)
