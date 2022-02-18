import geopandas
import h5py
import datetime

class Hdf5Writer:

    def __init__(self, filename, parameters=None):
        self.filename = filename
        self.h5file = h5py.File(self.filename, mode='w')
        # add file attributes
        self.h5file.attrs['featureType'] = 'trajectory'
        self.h5file.attrs['title'] = "ATLAS/ICESat-2 SlideRule"
        self.h5file.attrs['reference'] = 'https://doi.org/10.5281/zenodo.5484048'
        self.h5file.attrs['date_created'] = datetime.datetime.now().isoformat()
        self.h5file.attrs['date_type'] = "UTC"
        self.h5file.attrs['time_type'] = "CCSDS UTC-A"
        # for each adjustable sliderule parameter
        for p in parameters:
            # try to get the parameter if available
            try:
                self.h5file.attrs[p] = parameters[p]
            except:
                # if empty or unavailable
                pass
        # save each region as a list attribute
        if "poly" in parameters:
            lon,lat = io.from_region(parameters["poly"])
            self.h5file.attrs['poly_x'] = lon.copy()
            self.h5file.attrs['poly_y'] = lat.copy()

    def run(self, resource, result):
        grp = self.h5file.create_group(resource[:-3])
        # convert geodataframe to pandas dataframe
        df = geopandas.pd.DataFrame(result.drop(columns='geometry'))
        df['latitude'] = result['geometry'].values.y
        df['longitude'] = result['geometry'].values.x
        # create dataset for each variable in the dataframe
        for key,val in df.items():
            grp.create_dataset(key, val.shape, data=val, dtype=val.dtype, compression='gzip')
        # set coordinate reference system as attribute
        if result.crs:
            grp.attrs['crs'] = str(result.crs)
        else:
            grp.attrs['crs'] = 'EPSG:4326'

    def finish(self):
        if self.h5file != None:
            self.h5file.close()
            self.h5file = None

    def __del__(self):
        self.finish()
