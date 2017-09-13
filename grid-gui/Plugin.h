// ======================================================================
/*!
 * \brief SmartMet Grid Gui plugin interface
 */
// ======================================================================

#pragma once
#include <spine/SmartMetPlugin.h>
#include <spine/Reactor.h>
#include <spine/HTTP.h>
#include <libconfig.h++>
#include "grid-content/contentServer/http/server/ServerInterface.h"
#include "grid-content/contentServer/redis/RedisImplementation.h"
#include "engines/grid/Engine.h"


namespace SmartMet
{
namespace Plugin
{
namespace GridGui
{


class Plugin : public SmartMetPlugin, private boost::noncopyable
{
  public:

    Plugin(SmartMet::Spine::Reactor* theReactor, const char* theConfig);
    virtual ~Plugin();

    const std::string& getPluginName() const;
    int getRequiredAPIVersion() const;
    bool queryIsFast(const SmartMet::Spine::HTTP::Request& theRequest) const;

  protected:

    void init();
    void shutdown();
    void requestHandler(SmartMet::Spine::Reactor& theReactor,
                      const SmartMet::Spine::HTTP::Request& theRequest,
                      SmartMet::Spine::HTTP::Response& theResponse);

  private:

    Plugin();
    bool request(SmartMet::Spine::Reactor& theReactor,
                      const SmartMet::Spine::HTTP::Request& theRequest,
                      SmartMet::Spine::HTTP::Response& theResponse);

    bool page_info(SmartMet::Spine::Reactor& theReactor,
                      const SmartMet::Spine::HTTP::Request& theRequest,
                      SmartMet::Spine::HTTP::Response& theResponse);

    bool page_value(SmartMet::Spine::Reactor& theReactor,
                      const SmartMet::Spine::HTTP::Request& theRequest,
                      SmartMet::Spine::HTTP::Response& theResponse);

    bool page_timeseries(SmartMet::Spine::Reactor& theReactor,
                      const SmartMet::Spine::HTTP::Request& theRequest,
                      SmartMet::Spine::HTTP::Response& theResponse);

    bool page_table(SmartMet::Spine::Reactor& theReactor,
                      const SmartMet::Spine::HTTP::Request& theRequest,
                      SmartMet::Spine::HTTP::Response& theResponse);

    bool page_image(SmartMet::Spine::Reactor& theReactor,
                      const SmartMet::Spine::HTTP::Request& theRequest,
                      SmartMet::Spine::HTTP::Response& theResponse);

    bool page_map(SmartMet::Spine::Reactor& theReactor,
                      const SmartMet::Spine::HTTP::Request& theRequest,
                      SmartMet::Spine::HTTP::Response& theResponse);

    bool page_main(SmartMet::Spine::Reactor& theReactor,
                      const SmartMet::Spine::HTTP::Request& theRequest,
                      SmartMet::Spine::HTTP::Response& theResponse);

    void saveImage(const char *imageFile,
                      T::GridData&  gridData,
                      unsigned char hue,
                      unsigned char saturation,
                      unsigned char blur,
                      bool rotate);

    void saveMap(const char *imageFile,
                      uint columns,
                      uint rows,
                      T::ParamValue_vec& values,
                      unsigned char hue,
                      unsigned char saturation,
                      unsigned char blur);

    void saveTimeSeries(const char *imageFile,std::vector<T::ParamValue>& valueList,int idx,std::set<int> dayIdx);


    Engine::Grid::Engine*     itsGridEngine;
    const std::string         itsModuleName;
    SmartMet::Spine::Reactor* itsReactor;
    libconfig::Config         itsConfig;
    std::string               itsGridConfigDirectory;


};  // class Plugin

}  // namespace GridContent
}  // namespace Plugin
}  // namespace SmartMet

// ======================================================================
