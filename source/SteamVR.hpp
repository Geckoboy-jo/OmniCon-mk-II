#include "top.hpp"

class SteamVR {
public:
    void LaunchVR(int steamAppId)
    {
        std::system("flatpak run org.alvr.ALVR &");

        std::string steamCmd =
            "steam steam://rungameid/" +
            std::to_string(steamAppId) +
            " &";

        std::system(steamCmd.c_str());
    }
};