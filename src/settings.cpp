#include "settings.h"
#include "defines.h"


std::string Settings::filename = "noods.ini";

int Settings::directBoot = 1;
int Settings::fpsLimiter = 1;
int Settings::threaded2D = 0;
int Settings::threaded3D = 0;
int Settings::highRes3D  = 0;

std::string Settings::bios9Path    = "bios9.bin";
std::string Settings::bios7Path    = "bios7.bin";
std::string Settings::firmwarePath = "firmware.bin";
std::string Settings::gbaBiosPath  = "gba_bios.bin";
std::string Settings::sdImagePath  = "sd.img";


std::vector<Setting> Settings::settings = {
    Setting("directBoot", &directBoot, true),    Setting("fpsLimiter", &fpsLimiter, true),
    Setting("threaded2D", &threaded2D, false),   Setting("threaded3D", &threaded3D, false),
    Setting("highRes3D", &highRes3D, false),     Setting("bios9Path", &bios9Path, false),
    Setting("bios7Path", &bios7Path, false),     Setting("firmwarePath", &firmwarePath, false),
    Setting("gbaBiosPath", &gbaBiosPath, false), Setting("sdImagePath", &sdImagePath, false)};

void Settings::add(std::vector<Setting> platformSettings)
{
    settings.insert(settings.end(), platformSettings.begin(), platformSettings.end());
}
bool Settings::load(std::string filename)
{
    Settings::filename = filename;
    FILE *settingsFile = fopen(filename.c_str(), "r");
    if (!settingsFile)
        return false;
    char data[1024];
    while (fgets(data, 1024, settingsFile) != NULL) {
        std::string line  = data;
        int         split = line.find("=");
        std::string name  = line.substr(0, split);
        for (unsigned int i = 0; i < settings.size(); i++) {
            if (name == settings[i].name) {
                std::string value = line.substr(split + 1, line.size() - split - 2);
                if (settings[i].isString)
                    *(std::string *)settings[i].value = value;
                else if (value[0] >= 0x30 && value[0] <= 0x39)
                    *(int *)settings[i].value = stoi(value);
                break;
            }
        }
    }
    fclose(settingsFile);
    return true;
}
bool Settings::save()
{
    FILE *settingsFile = fopen(filename.c_str(), "w");
    if (!settingsFile)
        return false;
    for (unsigned int i = 0; i < settings.size(); i++) {
        std::string value =
            settings[i].isString ? *(std::string *)settings[i].value : std::to_string(*(int *)settings[i].value);
        fprintf(settingsFile, "%s=%s\n", settings[i].name.c_str(), value.c_str());
    }
    fclose(settingsFile);
    return true;
}
