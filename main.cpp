#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>

using namespace std;

static const string KSYSGUARDD_VERSION = "1.2.0";
static const string KSYSGUARDD = "ksysguardd";
static const string KSYSGUARDD_PROMPT = format("{}> ", KSYSGUARDD);
static const string MONITORS = "monitors";
static const string POWERCAP_DIRPATH = "/sys/class/powercap";
static const filesystem::path POWERCAP_DIR{POWERCAP_DIRPATH};
static const string energy_uj_filename = "energy_uj";
static const string name_filename = "name";

string readFileContentsAsString(string filepath)
{
    auto ifStream = ifstream(filepath);
    string fileContents;
    ifStream >> fileContents;
    ifStream.close();
    return fileContents;
}

uint64_t readFileContentsAsUint(string filepath)
{
    auto ifStream = ifstream(filepath);
    uint64_t fileContents;
    ifStream >> fileContents;
    ifStream.close();
    return fileContents;
}

class SensorType
{
public:
    virtual string getStringRep() = 0;
};

class IntegerSensorType : public SensorType
{
public:
    string getStringRep() { return "integer"; }
};

class FloatSensorType : public SensorType
{
public:
    string getStringRep() { return "float"; }
};

class Sensor
{
public:
    string name;
    SensorType *sensorType;
    virtual string readValue() = 0;
};

struct EnergyReading
{
    uint64_t energy_uj;
    time_t measurementTime;
};

//todo: Check how it works with suspend and resume, deal with that
// Reads energy info from Linux kernel powercap framework, and provides instantaneous power use
class PowerCapEnergySensor : public Sensor
{
    const string powercapName;
    EnergyReading lastEnergyReading;
    float lastPowerMeasurement;

    EnergyReading readEnergyValue()
    {
        auto energy_uj_path = filesystem::path(POWERCAP_DIR / powercapName / energy_uj_filename);
        EnergyReading energyReading;
        energyReading.energy_uj = readFileContentsAsUint(energy_uj_path.c_str());
        time(&energyReading.measurementTime);
        return energyReading;
    }

    float calculatePowerUse(EnergyReading lastEnergyReading, EnergyReading newEnergyReading)
    {
        uint64_t energyDiff = (newEnergyReading.energy_uj - lastEnergyReading.energy_uj) / 1e6;
        time_t timeDiff = newEnergyReading.measurementTime - lastEnergyReading.measurementTime;
        if (timeDiff <= 0)
            return 0;
        return energyDiff / timeDiff;
    }

    void updateEnergyValueLoop()
    {
        while (1) {
            this_thread::sleep_for(1s);
            auto newEnergyReading = readEnergyValue();
            lastPowerMeasurement = calculatePowerUse(lastEnergyReading, newEnergyReading);
            lastEnergyReading = newEnergyReading;
        }
    }

public:
    PowerCapEnergySensor(string pcapName, string sensorName)
        : powercapName(pcapName)
    {
        name = sensorName;
        sensorType = new FloatSensorType();
        lastEnergyReading = readEnergyValue();
        thread t1(&PowerCapEnergySensor::updateEnergyValueLoop, this);
        t1.detach();
    }

    string readValue() { return format("{}", lastPowerMeasurement); }
};

static std::map<string, Sensor *> sensorMap;

static void populateSensorMap()
{
    //check if powercap directory exists - if not, write to log that it doesn't and return
    auto powercapDirectoryEntry = filesystem::directory_entry(POWERCAP_DIRPATH);
    if (!powercapDirectoryEntry.exists()) {
        //todo: Print to log that the powercap directory doesn't exist or is inaccessible
        return;
    }
    //iterate all of the directory entries in powercap directory - filter out the ones that don't have an energy_uj file
    for (auto const &dirEntry : filesystem::directory_iterator(POWERCAP_DIRPATH)) {
        if (!dirEntry.is_directory())
            continue;
        auto energy_uj_dirEntry = filesystem::directory_entry(dirEntry.path() / energy_uj_filename);
        auto name_dirEntry = filesystem::directory_entry(dirEntry.path() / name_filename);
        if (!energy_uj_dirEntry.exists() || !name_dirEntry.exists())
            continue;

        //Read the powercap sensor name from the file and store in a variable
        auto sensorName = readFileContentsAsString(name_dirEntry.path().c_str());
        //finally populate the sensor map with the valid powercap directories
        auto newSensor = new PowerCapEnergySensor(dirEntry.path().filename(), sensorName);
        sensorMap[sensorName] = newSensor;
    }
}

int main()
{
    populateSensorMap();

    cout << KSYSGUARDD << " " << KSYSGUARDD_VERSION << endl;

    string input_command;
    while (1) {
        cout << KSYSGUARDD_PROMPT;
        cin >> input_command;
        if (input_command == MONITORS) {
            //Print out all of the available sensors along with type
            for (auto sensorEntry : sensorMap) {
                cout << sensorEntry.first << '\t' << sensorEntry.second->sensorType->getStringRep()
                     << endl;
            }
        } else if (sensorMap.contains(input_command)) {
            //Print out the current value for this sensor
            cout << sensorMap.at(input_command)->readValue() << endl;
        } else {
            //todo: Add else if where we check if input command is of the format sensorname? which means we have to print sensor metadata
        }
    }

    return 0;
}
