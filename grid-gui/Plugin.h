// ======================================================================
/*!
 * \brief SmartMet Grid Gui plugin interface
 */
// ======================================================================

#pragma once

#include "ColorMapFile.h"
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

struct ImagePaintParameters
{
  std::string imageFile;
  uint fileId = 0;
  uint messageIndex = 0;
  T::GeometryId geometryId = 0;
  T::GeometryId projectionId = 0;
  bool zeroIsMissing = false;
  unsigned char paint_alpha = 255;
  std::string   paint_colorMapName;
  unsigned char paint_hue = 0;
  unsigned char paint_saturation = 0;
  unsigned char paint_blur = 1;
  int  stream_step = 10;
  int  stream_minLength = 6;
  int  stream_maxLength = 64;
  uint stream_color = 0x808080;
  bool stream_animation = false;
  uint landBorder_color = 0xFF808080;
  uint landColor = 0xFF808080;
  uint landColor_position = 1;
  uint landShading_light = 128;
  uint landShading_shadow = 160;
  uint landShading_position = 2;
  uint seaColor = 0xFF0000FF;
  uint seaColor_position = 1;
  uint seaShading_light = 128;
  uint seaShading_shadow = 160;
  uint seaShading_position = 2;
  uint coordinateLine_color = 0xFFC0C0C0;
};




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
    bool isAdminQuery(const Spine::HTTP::Request& theRequest) const;

  protected:

    void init();
    void shutdown();
    void requestHandler(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse);

  private:

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

    int page_streams(Spine::Reactor& theReactor,
                      const Spine::HTTP::Request& theRequest,
                      Spine::HTTP::Response& theResponse,
                      Session& session);

    int page_streamsAnimation(Spine::Reactor& theReactor,
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


    void saveImage(ImagePaintParameters& params,int width,int height,
                      T::ParamValue_vec& values,
                      T::Coordinate_vec& coordinates,
                      T::Coordinate_vec *lineCoordinates);

    void saveImage(ImagePaintParameters& params);

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


    T::ColorMapFile*  getColorMapFile(std::string colorMapName);

    void checkImageCache();
    void getGenerations(T::GenerationInfoList& generationInfoList,std::set<std::string>& generations);
    void getLevelIds(T::ContentInfoList& contentInfoList,std::set<int>& levelIds);
    void getLevels(T::ContentInfoList& contentInfoList,int levelId,std::set<int>& levels);
    std::string getFmiKey(std::string& producerName,T::ContentInfo& contentInfo);
    void getForecastTypes(T::ContentInfoList& contentInfoList,int levelId,int level,std::set<int>& forecastTypes);
    void getForecastNumbers(T::ContentInfoList& contentInfoList,int levelId,int level,int forecastType,std::set<int>& forecastNumbers);
    void getGeometries(T::ContentInfoList& contentInfoList,int levelId,int level,int forecastType,int forecastNumber,std::set<int>& geometries);
    uint getColorValue(std::string& colorName);
    void initSession(Session& session);
    void loadColorFile();
    void loadProducerFile();
    bool loadImage(const char *fname,Spine::HTTP::Response &theResponse);

  private:

    const std::string         itsModuleName;
    Spine::Reactor*           itsReactor;
    ConfigurationFile         itsConfigurationFile;
    std::string               itsGridConfigFile;
    string_vec                itsColorMapFileNames;
    T::ColorMapFile_vec       itsColorMapFiles;
    std::string               itsColorFile;
    Colors                    itsColors;
    time_t                    itsColors_lastModified;
    std::string               itsImageCache_dir;
    uint                      itsImageCache_maxImages;
    uint                      itsImageCache_minImages;
    bool                      itsAnimationEnabled;
    std::string               itsImagesUnderConstruction[100];
    uint                      itsImageCounter;
    ThreadLock                itsThreadLock;
    std::string               itsProducerFile;
    time_t                    itsProducerFile_modificationTime;
    std::set<std::string>     itsProducerList;
    std::set<int>             itsBlockedProjections;

    std::shared_ptr<Engine::Grid::Engine> itsGridEngine;
    std::map<std::string,std::string>      itsImages;
};  // class Plugin

}  // namespace GridGui
}  // namespace Plugin
}  // namespace SmartMet

// ======================================================================
