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


namespace SmartMet
{
namespace Plugin
{
namespace GridGui
{


typedef std::vector<std::pair<std::string,unsigned int>> Colors;


class Plugin : public SmartMetPlugin, private boost::noncopyable
{
  public:

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

    Plugin();

    bool isLand(double lon,double lat);

    bool request(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse);

    bool page_info(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse);

    bool page_value(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse);

    bool page_timeseries(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse);

    bool page_table(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse);

    bool page_coordinates(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse);

    bool page_image(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse);

    bool page_locations(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse);

    bool page_symbols(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse);

    bool page_map(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse);

    bool page_main(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse);


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
                      uint landBorder,
                      std::string landMask,
                      std::string seaMask,
                      std::string colorMapName,
                      T::GeometryId geometryId,
                      std::string symbolMap,
                      std::string locations,
                      bool showSymbols);

    void saveImage(const char *imageFile,
                      uint fileId,
                      uint messageIndex,
                      unsigned char hue,
                      unsigned char saturation,
                      unsigned char blur,
                      uint coordinateLines,
                      uint landBorder,
                      std::string landMask,
                      std::string seaMask,
                      std::string colorMapName,
                      T::GeometryId geometryId,
                      T::GeometryId projectionId,
                      std::string symbolMap,
                      std::string locations,
                      bool showSymbols);

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
                      std::string colorMapName);

    void saveTimeSeries(const char *imageFile,std::vector<T::ParamValue>& valueList,int idx,std::set<int> dayIdx);

    T::ColorMapFile*  getColorMapFile(std::string colorMapName);
    T::SymbolMapFile* getSymbolMapFile(std::string symbolMap);
    T::LocationFile*  getLocationFile(std::string name);


    void checkImageCache();
    void getGenerations(T::GenerationInfoList& generationInfoList,std::set<std::string>& generations);
    void getLevelIds(T::ContentInfoList& contentInfoList,std::set<int>& levelIds);
    void getLevels(T::ContentInfoList& contentInfoList,int levelId,std::set<int>& levels);
    void getForecastTypes(T::ContentInfoList& contentInfoList,int levelId,int level,std::set<int>& forecastTypes);
    void getForecastNumbers(T::ContentInfoList& contentInfoList,int levelId,int level,int forecastType,std::set<int>& forecastNumbers);
    void getGeometries(T::ContentInfoList& contentInfoList,int levelId,int level,int forecastType,int forecastNumber,std::set<int>& geometries);
    uint getColorValue(std::string& colorName);
    void loadColorFile();
    void loadImage(const char *fname,Spine::HTTP::Response &theResponse);


    Engine::Grid::Engine*     itsGridEngine;
    const std::string         itsModuleName;
    Spine::Reactor*           itsReactor;
    ConfigurationFile         itsConfigurationFile;
    std::string               itsGridConfigFile;
    std::string               itsLandSeaMaskFile;
    CImage                    itsLandSeaMask;
    string_vec                itsColorMapFileNames;
    string_vec                itsLocationFileNames;
    string_vec                itsSymbolMapFileNames;
    T::ColorMapFile_vec       itsColorMapFiles;
    T::LocationFile_vec       itsLocationFiles;
    T::SymbolMapFile_vec      itsSymbolMapFiles;
    std::string               itsColorFile;
    Colors                    itsColors;
    time_t                    itsColors_lastModified;
    std::string               itsImageCache_dir;
    uint                      itsImageCache_maxImages;
    uint                      itsImageCache_minImages;
    bool                      itsAnimationEnabled;

    std::map<std::string,std::string> itsImages;

};  // class Plugin

}  // namespace GridGui
}  // namespace Plugin
}  // namespace SmartMet

// ======================================================================
