#include "audioplugin.h"
#include "errorhandling.h"
#include <openmha/mha_algo_comm.hh>
#include <openmha/mhapluginloader.h>
#include "coordinates.h"

class openmha_t : public TASCAR::audioplugin_base_t, public MHAKernel::algo_comm_class_t {
public:
  openmha_t( const TASCAR::audioplugin_cfg_t& cfg );
  void ap_process(std::vector<TASCAR::wave_t>& chunk, const TASCAR::pos_t& pos, const TASCAR::zyx_euler_t&, const TASCAR::transport_t& tp);
  void configure();
  void release();
  void add_variables( TASCAR::osc_server_t* srv );
  static int osc_parse(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data);
  void parse( const std::string& s );
  ~openmha_t();
private:
  std::string plugin;
  std::string config;
  PluginLoader::mhapluginloader_t mhaplug;
  MHASignal::waveform_t* sIn;
  MHA_AC::waveform_t acpos;
};

openmha_t::openmha_t( const TASCAR::audioplugin_cfg_t& cfg )
  : audioplugin_base_t( cfg ),
    plugin(e->get_attribute_value("plugin")),
    config(e->get_attribute_value("config")),
    mhaplug(get_c_handle(),plugin),
    sIn(NULL),
    acpos(get_c_handle(),"pos",3,1,true)
{
  GET_ATTRIBUTE(plugin);
  GET_ATTRIBUTE(config);
  if( !config.empty() )
    std::cout << mhaplug.parse( config ) << std::endl;
  std::stringstream scfg(TASCAR::xml_get_text(e,""));
  while(!scfg.eof() ){
    std::string cfgline;
    getline(scfg,cfgline,'\n');
    MHAParser::trim(cfgline);
    if( !cfgline.empty() ){
      std::cout << mhaplug.parse( cfgline ) << std::endl;
    }
  }
}

int openmha_t::osc_parse(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data)
{
  ((openmha_t*)user_data)->parse(&(argv[0]->s));
  return 0;
}

void openmha_t::parse(const std::string& s)
{
  try{
    std::cout << mhaplug.parse( s ) << std::endl;
  }
  catch( const std::exception& e ){
    std::string msg("openMHA Error: ");
    msg+=e.what();
    TASCAR::add_warning(msg);
  }
}

void openmha_t::add_variables( TASCAR::osc_server_t* srv )
{
  srv->add_method("/parse","s",osc_parse,this);
}

void openmha_t::configure()
{
  audioplugin_base_t::configure();
  mhaconfig_t mhacfg;
  mhacfg.channels = n_channels;
  mhacfg.domain = MHA_WAVEFORM;
  mhacfg.fragsize = n_fragment;
  mhacfg.wndlen = n_fragment;
  mhacfg.fftlen = n_fragment;
  mhacfg.srate = f_sample;
  mhaconfig_t mhacfg_out(mhacfg);
  mhaplug.prepare( mhacfg );
  PluginLoader::mhaconfig_compare(mhacfg_out,mhacfg,"openmha");
  sIn = new MHASignal::waveform_t( n_fragment, n_channels );
}

void openmha_t::release()
{
  mhaplug.release();
  audioplugin_base_t::release();
  delete sIn;
}

openmha_t::~openmha_t()
{
}

void openmha_t::ap_process(std::vector<TASCAR::wave_t>& chunk, const TASCAR::pos_t& pos, const TASCAR::zyx_euler_t&, const TASCAR::transport_t& tp)
{
  try{
    // copy sound vertex position:
    acpos.buf[0] = pos.x;
    acpos.buf[1] = pos.y;
    acpos.buf[2] = pos.z;
    // copy non-interleaved TASCAR data to interleaved openMHA data:
    for(uint32_t k=0;k<n_channels;++k)
      chunk[k].copy_to_stride( &(sIn->buf[k]), sIn->num_frames, n_channels );
    // process audio in openMHA:
    mha_wave_t* sOut(NULL);
    mhaplug.process( sIn, &sOut );
    // copy interleaved openMHA data back to non-interleaved TASCAR data:
    for(uint32_t k=0;k<n_channels;++k)
      chunk[k].copy_stride( &(sOut->buf[k]), sOut->num_frames, n_channels );
  }
  catch( const std::exception& e ){
    std::cerr << "Error (process): " << e.what() << std::endl;
  }
}

REGISTER_AUDIOPLUGIN(openmha_t);

/*
 * Local Variables:
 * mode: c++
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * compile-command: "make -C .."
 * End:
 */
