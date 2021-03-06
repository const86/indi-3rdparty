/*******************************************************************************
  Copyright(c) 2015 Jasem Mutlaq. All rights reserved.
  Copyright(c) 2015 Paweł T. Jochym  <jochym AT gmail DOT com>
..Copyright(c) 2014 Radek Kaczorek  <rkaczorek AT gmail DOT com>
  Based on Simple GPS Simulator by Jasem Mutlaq

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the Free
  Software Foundation; either version 2 of the License, or (at your option)
  any later version.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU Library General Public License
  along with this library; see the file COPYING.LIB.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301, USA.

  The full GNU General Public License is included in this distribution in the
  file called LICENSE.
*******************************************************************************/

#include "gps_driver.h"

#include "config.h"

#include <string.h>
#include <libgpsmm.h>

#include <libnova/julian_day.h>
#include <libnova/sidereal_time.h>

#include <memory>

// We declare an auto pointer to GPSD.
static std::unique_ptr<GPSD> gpsd(new GPSD());

void ISGetProperties(const char *dev)
{
    gpsd->ISGetProperties(dev);
}

void ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int num)
{
    gpsd->ISNewSwitch(dev, name, states, names, num);
}

void ISNewText(const char *dev, const char *name, char *texts[], char *names[], int num)
{
    gpsd->ISNewText(dev, name, texts, names, num);
}

void ISNewNumber(const char *dev, const char *name, double values[], char *names[], int num)
{
    gpsd->ISNewNumber(dev, name, values, names, num);
}

void ISNewBLOB(const char *dev, const char *name, int sizes[], int blobsizes[], char *blobs[], char *formats[],
               char *names[], int n)
{
    INDI_UNUSED(dev);
    INDI_UNUSED(name);
    INDI_UNUSED(sizes);
    INDI_UNUSED(blobsizes);
    INDI_UNUSED(blobs);
    INDI_UNUSED(formats);
    INDI_UNUSED(names);
    INDI_UNUSED(n);
}

void ISSnoopDevice(XMLEle *root)
{
    INDI_UNUSED(root);
}

GPSD::GPSD()
{
    setVersion(GPSD_VERSION_MAJOR, GPSD_VERSION_MINOR);
}

const char *GPSD::getDefaultName()
{
    return "GPSD";
}

bool GPSD::Connect()
{
    if (gps == nullptr)
    {
        gps = new gpsmm("localhost", DEFAULT_GPSD_PORT);
    }
    if (gps->stream(WATCH_ENABLE | WATCH_JSON) == nullptr)
    {
        LOG_WARN("No GPSD running.");
        return false;
    }
    return true;
}

bool GPSD::Disconnect()
{
    delete gps;
    gps = nullptr;
    LOG_INFO("GPS disconnected successfully.");
    return true;
}

bool GPSD::initProperties()
{
    // We init parent properties first
    INDI::GPS::initProperties();

    IUFillText(&GPSstatusT[0], "GPS_FIX", "Fix Mode", nullptr);
    IUFillTextVector(&GPSstatusTP, GPSstatusT, 1, getDeviceName(), "GPS_STATUS", "GPS Status", MAIN_CONTROL_TAB, IP_RO,
                     60, IPS_IDLE);

    IUFillNumber(&PolarisN[0], "HA", "Polaris Hour Angle", "%010.6m", 0, 24, 0, 0.0);
    IUFillNumberVector(&PolarisNP, PolarisN, 1, getDeviceName(), "POLARIS", "Polaris", MAIN_CONTROL_TAB, IP_RO, 60,
                       IPS_IDLE);

    // Whether to use the system time or gps actual time
    IUFillSwitch(&TimeSourceS[TS_GPS], "TS_GPS", "GPS", ISS_ON);
    IUFillSwitch(&TimeSourceS[TS_SYSTEM], "TS_SYSTEM", "System", ISS_OFF);
    IUFillSwitchVector(&TimeSourceSP, TimeSourceS, 2, getDeviceName(), "GPS_TIME_SOURCE", "Time Source",
                       OPTIONS_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);

    // Location to be used if no GPS is available
    IUFillNumber(&SimLocationN[LOCATION_LATITUDE], "SIM_LAT", "Lat (dd:mm:ss)", "%010.6m", -90, 90, 0, 29.1);
    IUFillNumber(&SimLocationN[LOCATION_LONGITUDE], "SIM_LONG", "Lon (dd:mm:ss)", "%010.6m", 0, 360, 0, 48.5);
    IUFillNumber(&SimLocationN[LOCATION_ELEVATION], "SIM_ELEV", "Elevation (m)", "%g", -200, 10000, 0, 12);
    IUFillNumberVector(&SimLocationNP, SimLocationN, 3, getDeviceName(), "SIM_GEOGRAPHIC_COORD", "Simulated Location", OPTIONS_TAB,
                       IP_RW, 60, IPS_IDLE);

    addAuxControls();

    setDriverInterface(GPS_INTERFACE | AUX_INTERFACE);

    return true;
}

bool GPSD::updateProperties()
{
    // Call parent update properties first
    INDI::GPS::updateProperties();

    if (isConnected())
    {
        defineText(&GPSstatusTP);
        defineNumber(&PolarisNP);
        defineSwitch(&TimeSourceSP);
        defineNumber(&SimLocationNP);
    }
    else
    {
        // We're disconnected
        deleteProperty(GPSstatusTP.name);
        deleteProperty(PolarisNP.name);
        deleteProperty(TimeSourceSP.name);
        deleteProperty(SimLocationNP.name);
    }
    return true;
}
bool GPSD::setSystemTime(time_t& raw_time)
{
    #ifdef __linux__
        #if defined(__GNU_LIBRARY__)
            #if (__GLIBC__ >= 2) && (__GLIBC_MINOR__ > 30)
                timespec sTime = {};
                sTime.tv_sec = raw_time;
                clock_settime(CLOCK_REALTIME, &sTime);
            #else
                stime(&raw_time);
            #endif
        #else
            stime(&raw_time);
        #endif
    #endif
    return true;
}

IPState GPSD::updateGPS()
{
    // Indicate gps refresh in progress
    if (TimeTP.s != IPS_BUSY)
    {
        TimeTP.s = IPS_BUSY;
        IDSetText(&TimeTP, nullptr);
    }

    if (LocationNP.s != IPS_BUSY)
    {
        LocationNP.s = IPS_BUSY;
        IDSetNumber(&LocationNP, nullptr);
    }

    if (GPSstatusTP.s != IPS_BUSY)
    {
        GPSstatusTP.s = IPS_BUSY;
        IDSetText(&GPSstatusTP, nullptr);
    }

    if (PolarisNP.s != IPS_BUSY)
    {
        PolarisNP.s = IPS_BUSY;
        IDSetNumber(&PolarisNP, nullptr);
    }

    if (RefreshSP.s != IPS_BUSY)
    {
        RefreshSP.s = IPS_BUSY;
        IDSetSwitch(&RefreshSP, nullptr);
    }

#if GPSD_API_MAJOR_VERSION >= 10
    struct gps_fix_t *gpsFix;
#endif
    struct gps_data_t *gpsData;
    time_t raw_time;

    if (isSimulation() || IUFindOnSwitchIndex(&TimeSourceSP) == TS_SYSTEM)
    {
        // Update time regardless having gps fix.
        // We are using system time assuming the system is synced with the gps
        // by gpsd using chronyd or ntpd.
        char ts[32] = {0};
        // To get time from gps fix we can use
        // raw_time = gpsData->fix.time;
        // but this will remove all sophistication of ntp etc.
        // Better just use the best estimation the system can provide.

        time(&raw_time);

        setSystemTime(raw_time);

        struct tm *utc = gmtime(&raw_time);
        strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", utc);
        IUSaveText(&TimeT[0], ts);

        struct tm *local = localtime(&raw_time);
        snprintf(ts, sizeof(ts), "%4.2f", (local->tm_gmtoff / 3600.0));
        IUSaveText(&TimeT[1], ts);

        TimeTP.s = IPS_OK;
    }

    if (isSimulation())
    {
        LocationNP.s                        = IPS_OK;
        LocationN[LOCATION_LATITUDE].value  = SimLocationN[LOCATION_LATITUDE].value;
        LocationN[LOCATION_LONGITUDE].value = SimLocationN[LOCATION_LONGITUDE].value;
        LocationN[LOCATION_ELEVATION].value = SimLocationN[LOCATION_ELEVATION].value;

        IDSetNumber(&LocationNP, nullptr);

        return IPS_OK;
    }

    if (!gps->waiting(1000))
    {
        if (GPSstatusTP.s != IPS_BUSY)
        {
            LOG_INFO("Waiting for gps data...");
            GPSstatusTP.s = IPS_BUSY;
        }
        return IPS_BUSY;
    }

    // Empty the buffer and keep only the last data block
    while (1)
    {
        if ((gpsData = gps->read()) == nullptr)
        {
            LOG_ERROR("GPSD read error.");
            IDSetText(&GPSstatusTP, nullptr);
            return IPS_ALERT;
        }
        // Exit the loop if there is no more data in the buffer
        if (!gps->waiting(0))
            break;
    }

#if GPSD_API_MAJOR_VERSION >= 10
    if (gpsFix->status == STATUS_NO_FIX)
#else  
    if (gpsData->status == STATUS_NO_FIX)
#endif
    {
        // We have no fix and there is no point in further processing.
        IUSaveText(&GPSstatusT[0], "NO FIX");
        if (GPSstatusTP.s == IPS_OK)
        {
            LOG_WARN("GPS fix lost.");
        }
        GPSstatusTP.s = IPS_BUSY;
        IDSetText(&GPSstatusTP, nullptr);
        return IPS_BUSY;
    }
    else
    {
        // We may have a fix. Check if fix structure contains proper fix.
        // We require at least 2D fix - the altitude is not so crucial (?)
        if (gpsData->fix.mode < MODE_2D)
        {
            // The position is not realy measured yet - we have no valid data
            // Keep looking
            IUSaveText(&GPSstatusT[0], "NO FIX");
            if (GPSstatusTP.s == IPS_OK)
            {
                LOG_WARN("GPS fix lost.");
            }
            GPSstatusTP.s = IPS_BUSY;
            IDSetText(&GPSstatusTP, nullptr);
            return IPS_BUSY;
        }
    }

    // detect gps fix showing up after not being avaliable
    if (GPSstatusTP.s != IPS_OK)
        LOG_INFO("GPS fix obtained.");

    // update gps fix status
    if (gpsData->fix.mode == MODE_3D)
    {
        IUSaveText(&GPSstatusT[0], "3D FIX");
        GPSstatusTP.s      = IPS_OK;
        IDSetText(&GPSstatusTP, nullptr);
    }
    else if (gpsData->fix.mode == MODE_2D)
    {
        IUSaveText(&GPSstatusT[0], "2D FIX");
        GPSstatusTP.s      = IPS_OK;
        IDSetText(&GPSstatusTP, nullptr);
    }
    else
    {
        IUSaveText(&GPSstatusT[0], "NO FIX");
        GPSstatusTP.s      = IPS_BUSY;
        IDSetText(&GPSstatusTP, nullptr);
        return IPS_BUSY;
    }

    // update gps location
    // we should have a gps fix data now
    // fprintf(stderr,"Fix: %d time: %lf\n", data->fix.mode, data->fix.time);

    LocationN[LOCATION_LATITUDE].value  = gpsData->fix.latitude;
    LocationN[LOCATION_LONGITUDE].value = gpsData->fix.longitude;
    // 2017-11-15 Jasem: INDI Longitude is 0 to 360 East+
    if (LocationN[LOCATION_LONGITUDE].value < 0)
        LocationN[LOCATION_LONGITUDE].value += 360;

    if (gpsData->fix.mode == MODE_3D)
    {
        LocationN[LOCATION_ELEVATION].value = gpsData->fix.altitude;
    }
    else
    {
        // Presume we are at sea level if we heve no elevation data
        LocationN[LOCATION_ELEVATION].value = 0;
    }
    LocationNP.s = IPS_OK;

    // Get Time from raw GPS source
    if (IUFindOnSwitchIndex(&TimeSourceSP) == TS_GPS)
    {
        char ts[32] = {0};
#if GPSD_API_MAJOR_VERSION < 9
        raw_time = gpsData->fix.time;
#else
        raw_time = gpsData->fix.time.tv_sec;
#endif
    
        setSystemTime(raw_time);

#if GPSD_API_MAJOR_VERSION < 9
        unix_to_iso8601(gpsData->fix.time, ts, 32);
#else
        timespec_to_iso8601(gpsData->fix.time, ts, 32);
#endif
        IUSaveText(&TimeT[0], ts);

        struct tm *local = localtime(&raw_time);
        snprintf(ts, sizeof(ts), "%4.2f", (local->tm_gmtoff / 3600.0));
        IUSaveText(&TimeT[1], ts);

        TimeTP.s = IPS_OK;
    }

    // calculate Polaris HA
    double jd, lst, polarislsrt;

    // polaris location - RA 02h 31m 47s DEC 89° 15' 50'' (J2000)
    jd  = ln_get_julian_from_timet(&raw_time);
    lst = ln_get_apparent_sidereal_time(jd);

    // Local Hour Angle = Local Sidereal Time - Polaris Right Ascension
    polarislsrt       = lst - 2.529722222 + (gpsData->fix.longitude / 15.0);
    PolarisN[0].value = polarislsrt;

    GPSstatusTP.s = IPS_OK;
    IDSetText(&GPSstatusTP, nullptr);
    PolarisNP.s = IPS_OK;
    IDSetNumber(&PolarisNP, nullptr);
    RefreshSP.s = IPS_OK;
    IDSetSwitch(&RefreshSP, nullptr);

    return IPS_OK;
}

bool GPSD::ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    if (dev != nullptr && !strcmp(dev, getDeviceName()))
    {
        if (!strcmp(name, TimeSourceSP.name))
        {
            IUUpdateSwitch(&TimeSourceSP, states, names, n);
            TimeSourceSP.s = IPS_OK;
            IDSetSwitch(&TimeSourceSP, nullptr);
            return true;
        }
    }

    return INDI::GPS::ISNewSwitch(dev, name, states, names, n);
}

bool GPSD::ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
    if (dev != nullptr && strcmp(dev, getDeviceName()) == 0)
    {
        ///////////////////////////////////
        // Geographic Coords
        ///////////////////////////////////
        if (strcmp(name, "SIM_GEOGRAPHIC_COORD") == 0)
        {
            int latindex       = IUFindIndex("SIM_LAT",  names, n);
            int longindex      = IUFindIndex("SIM_LONG", names, n);
            int elevationindex = IUFindIndex("SIM_ELEV", names, n);

            if (latindex == -1 || longindex == -1 || elevationindex == -1)
            {
                SimLocationNP.s = IPS_ALERT;
                IDSetNumber(&SimLocationNP, "Location data missing or corrupted.");
            }

            double latitude  = values[latindex];
            double longitude = values[longindex];
            double elevation = values[elevationindex];

            SimLocationNP.s                        = IPS_OK;
            SimLocationN[LOCATION_LATITUDE].value  = latitude;
            SimLocationN[LOCATION_LONGITUDE].value = longitude;
            SimLocationN[LOCATION_ELEVATION].value = elevation;

            //  Update client display
            IDSetNumber(&SimLocationNP, nullptr);
        }
    }

    return INDI::GPS::ISNewNumber(dev, name, values, names, n);
}


bool GPSD::saveConfigItems(FILE *fp)
{
    INDI::GPS::saveConfigItems(fp);

    IUSaveConfigSwitch(fp, &TimeSourceSP);
    IUSaveConfigNumber(fp, &SimLocationNP);

    return true;
}
