// ======================================================================
/*!
 * \brief SmartMet Grid Gui plugin interface
 */
// ======================================================================

#pragma once

#include "ColorMapFile.h"
#include "SymbolMapFile.h"
#include "LocationFile.h"
#include <spine/SmartMetPlugin.h>
#include <spine/Reactor.h>
#include <spine/HTTP.h>
#include <engines/grid/Engine.h>
#include <grid-files/common/ImageFunctions.h>
#include <grid-files/common/BitLine.h>
#include <grid-files/common/Session.h>


namespace SmartMet
{
namespace Plugin
{
namespace GridGui
{

typedef std::vector<std::pair<std::string,unsigned int>> Colors;



class Plugin : public SmartMetPlugin
{
  public:

    Plugin() = delete;
    Plugin(const Plugin& other) = delete;
    Plugin& operator=(const Plugin& other) = delete;
    Plugin(Spine::Reactor* theReactor, const char* theConfig);
    virtual ~Plugin();

    const std::string& getPluginName() const;
    int getRequiredAPIVersion() const;
    bool queryIsFast(const Spine::HTTP::Request& theRequest) const;

  protected:

    void init();
    void shutdown();
    void requestHandler(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse);

  private:

    bool isLand(double lon,double lat);

    int request(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse);

    int page_info(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_message(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_download(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_value(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_timeseries(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_table(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_coordinates(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_image(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_isolines(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_streams(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_streamsAnimation(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_locations(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_symbols(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_map(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_main(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);


    void saveImage(const char *imageFile,
                      int width,
                      int height,
                      T::ParamValue_vec& values,
                      T::Coordinate_vec& coordinates,
                      T::Coordinate_vec& lineCoordinates,
                      unsigned char hue,
                      unsigned char saturation,
                      unsigned char blur,
                      uint coordinateLines,
                      uint isolines,
                      std::string isolineValues,
                      uint landBorder,
                      std::string landMask,
                      std::string seaMask,
                      std::string colorMapName,
                      std::string missingStr,
                      T::GeometryId geometryId,
                      std::string symbolMap,
                      std::string locations,
                      bool showSymbols,
                      int pstep,
                      int minLength,
                      int maxLength,
                      bool lightBackground,
                      bool animation);

    void saveImage(const char *imageFile,
                      uint fileId,
                      uint messageIndex,
                      unsigned char hue,
                      unsigned char saturation,
                      unsigned char blur,
                      uint coordinateLines,
                      uint isolines,
                      std::string isolineValues,
                      uint landBorder,
                      std::string landMask,
                      std::string seaMask,
                      std::string colorMapName,
                      std::string missingStr,
                      T::GeometryId geometryId,
                      T::GeometryId projectionId,
                      std::string symbolMap,
                      std::string locations,
                      bool showSymbols,
                      int pstep,
                      int minLength,
                      int maxLength,
                      bool lightBackground,
                      bool animation);

    void saveMap(const char *imageFile,
                      uint columns,
                      uint rows,
                      T::ParamValue_vec& values,
                      unsigned char hue,
                      unsigned char saturation,
                      unsigned char blur,
                      uint coordinateLines,
                      uint landBorder,
                      std::string landMask,
                      std::string seaMask,
                      std::string colorMapName,
                      std::string missingStr);

    void saveTimeSeries(const char *imageFile,std::vector<T::ParamValue>& valueList,int idx,std::set<int> dayIdx);

    T::ColorMapFile*  getColorMapFile(std::string colorMapName);
    T::SymbolMapFile* getSymbolMapFile(std::string symbolMap);
    T::LocationFile*  getLocationFile(std::string name);


    void checkImageCache();
    void getGenerations(T::GenerationInfoList& generationInfoList,std::set<std::string>& generations);
    void getLevelIds(T::ContentInfoList& contentInfoList,std::set<int>& levelIds);
    void getLevels(T::ContentInfoList& contentInfoList,int levelId,std::set<int>& levels);
    std::string getFmiKey(std::string& producerName,T::ContentInfo& contentInfo);
    void getForecastTypes(T::ContentInfoList& contentInfoList,int levelId,int level,std::set<int>& forecastTypes);
    void getForecastNumbers(T::ContentInfoList& contentInfoList,int levelId,int level,int forecastType,std::set<int>& forecastNumbers);
    void getGeometries(T::ContentInfoList& contentInfoList,int levelId,int level,int forecastType,int forecastNumber,std::set<int>& geometries);
    uint getColorValue(std::string& colorName);
    T::ParamValue_vec getIsolineValues(std::string& isolineValues);
    void loadColorFile();
    //void loadDaliFile();
    void loadIsolineFile();
    bool loadImage(const char *fname,Spine::HTTP::Response &theResponse);
    void initSession(Session& session);


    Engine::Grid::Engine*     itsGridEngine;
    const std::string         itsModuleName;
    Spine::Reactor*           itsReactor;
    ConfigurationFile         itsConfigurationFile;
    std::string               itsGridConfigFile;
    //std::string               itsDaliFile;
    std::string               itsLandSeaMaskFile;
    BitLine                   itsLandSeaMask;
    uint                      itsLandSeaMask_width;
    uint                      itsLandSeaMask_height;
    string_vec                itsColorMapFileNames;
    string_vec                itsLocationFileNames;
    string_vec                itsSymbolMapFileNames;
    T::ColorMapFile_vec       itsColorMapFiles;
    T::LocationFile_vec       itsLocationFiles;
    T::SymbolMapFile_vec      itsSymbolMapFiles;
    std::string               itsIsolineFile;
    std::string               itsColorFile;
    Colors                    itsColors;
    time_t                    itsColors_lastModified;
    time_t                    itsDaliFile_lastModified;
    std::string               itsImageCache_dir;
    uint                      itsImageCache_maxImages;
    uint                      itsImageCache_minImages;
    bool                      itsAnimationEnabled;
    std::string               itsImagesUnderConstruction[100];
    uint                      itsImageCounter;
    ThreadLock                itsThreadLock;

    std::map<std::string,T::ParamValue_vec> itsIsolines;
    std::map<std::string,std::string>       itsImages;
    std::set<int>                           itsBlockedProjections;
    //std::vector<string_vec>                 itsDaliProducts;
};  // class Plugin

}  // namespace GridGui
}  // namespace Plugin
}  // namespace SmartMet

// ======================================================================
