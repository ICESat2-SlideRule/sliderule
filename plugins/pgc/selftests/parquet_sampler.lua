local runner = require("test_executive")
console = require("console")
asset = require("asset")
csv = require("csv")
json = require("json")

local td = runner.rootdir(arg[0])
local in_file = td.."test.parquet"
local out_file = td.."samples.parquet"

console.monitor:config(core.LOG, core.DEBUG)
sys.setlvl(core.LOG, core.DEBUG)

local assets = asset.loaddir()

-- Unit Test --

-- local demTypes = {"arcticdem-mosaic", "arcticdem-strips"}
local demTypes = {"arcticdem-mosaic"}
-- local demTypes = {"arcticdem-strips"}

-- Correct values test for different POIs

local sigma = 1.0e-9

for i = 1, #demTypes do
    local demType = demTypes[i];
    local dem = geo.raster(geo.parms({asset=demType, algorithm="NearestNeighbour", radius=30, zonal_stats=true}))
    -- local dem = geo.raster(geo.parms({asset=demType, algorithm="NearestNeighbour", radius=0}))

    runner.check(dem ~= nil)
    local err = dem:parquet_sample(in_file, out_file)
    runner.check(err == 0)
end

-- Report Results --

runner.report()

