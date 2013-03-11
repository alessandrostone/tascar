/**
   \file tascar_renderer.cc
   \ingroup apptascar
   \brief Scene rendering tool
   \author Giso Grimm, Carl-von-Ossietzky Universitaet Oldenburg
   \date 2013

   \section license License (GPL)

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; version 2 of the
   License.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.

*/

/*
  todo: diffuse reverb for enclosed sources, damping+attenuation for
  non-enclosed sources
  
  todo: directive sound sources - normal vector and size define
  directivity, use for sources and reflected objects

 */

#include "scene.h"
#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "tascar.h"
#include "amb33defs.h"
#include "async_file.h"
#include <getopt.h>
#include "osc_helper.h"

#define SUBSAMPLE 32

using namespace AMB33;
using namespace TASCAR;

namespace TASCAR {

  class trackpan_amb33_t {
  public:
    trackpan_amb33_t(double srate, uint32_t fragsize, double maxdist);
    trackpan_amb33_t(const trackpan_amb33_t&);
    ~trackpan_amb33_t();
  protected:
    varidelay_t delayline;
    double srate_;
    double dt_sample;
    double dt_update;
    uint32_t fragsize_;
    // ambisonic weights:
    float _w[idx::channels];
    float w_current[idx::channels];
    float dw[idx::channels];
    float d_current;
    float dd;
    float clp_current;
    float dclp;
    float y;
    float dscale;
    double maxdist;
  public:
    void process(uint32_t n, float* vIn, const std::vector<float*>& outBuffer, uint32_t tp_frame, double tp_time, bool tp_rolling, sound_t*);
  protected:
    void updatepar(pos_t);
  };

  class mirror_pan_t : public trackpan_amb33_t {
  public:
    mirror_pan_t(double srate, uint32_t fragsize, double maxdist);
    void process(uint32_t n, float* vIn, const std::vector<float*>& outBuffer, uint32_t tp_frame, double tp_time, bool tp_rolling, sound_t*,face_object_t*);
  private:
    float c1_current;
    float dc1;
    float c2_current;
    float dc2;
    float my;
  };

  class reverb_line_t {
  public:
    reverb_line_t(double srate, uint32_t fragsize, double maxdist);
    reverb_line_t(const reverb_line_t&);
  protected:
    varidelay_t delayline;
    double srate_;
    double dt_sample;
    double dt_update;
    uint32_t fragsize_;
    float d_global_current;
    float dd_global;
    float d_border_current;
    float dd_border;
    float clp_current;
    float dclp;
    float y;
    float dscale;
    double maxdist;
  public:
    void process(uint32_t n, float* vIn, float* vOut, uint32_t tp_frame, double tp_time, bool tp_rolling, sound_t*, diffuse_reverb_t* reverb);
  };
  
};

mirror_pan_t::mirror_pan_t(double srate, uint32_t fragsize, double maxdist_)
  : trackpan_amb33_t(srate,fragsize,maxdist_),
    c1_current(0),
    dc1(0),
    c2_current(0),
    dc2(0),
    my(0)
{
}

void mirror_pan_t::process(uint32_t n, float* vIn, const std::vector<float*>& outBuffer, uint32_t tp_frame, double tp_time, bool tp_rolling, sound_t* snd,face_object_t* face)
{
  if( n != fragsize_ )
    throw ErrMsg("fragsize changed");
  double ldt(dt_sample);
  if( !tp_rolling )
    ldt = 0;
  if( face->isactive(tp_time) && vIn ){
    double nexttime(tp_time+ldt*n);
    // first get sound position in global coordinates:
    pos_t psnd(snd->get_pos_global(nexttime));
    // then get mirror object:
    mirror_t m(face->get_mirror(nexttime,psnd));
    // now transform mirror source relative to listener:
    m.p -= snd->get_reference()->get_location(nexttime);
    m.p /= snd->get_reference()->get_orientation(nexttime);
    // update panner and low pass filters:
    updatepar(m.p);
    dc1 = (m.c1-c1_current)*dt_update;
    dc2 = (m.c2-c2_current)*dt_update;
    //DEBUG(m.p.print_cart());
    //DEBUG(m.c1);
    //DEBUG(m.c2);
    // iterate through fragment:
    for( unsigned int i=0;i<n;i++){
      delayline.push(vIn[i]);
      float d = (d_current+=dd);
      float d1 = 1.0/std::max(1.0f,d);
      if( tp_rolling ){
        float c1(clp_current+=dclp);
        float c2(1.0f-c1);
        float mc1(c1_current+=dc1);
        float mc2(c2_current+=dc2);
        y = c2*y+c1*delayline.get_dist(d)*d1;
        my = mc2*my+mc1*y;
        //my = y;
        for( unsigned int k=0;k<idx::channels;k++){
          outBuffer[k][i] += (w_current[k] += dw[k]) * my;
        }
      }
    }
  }
}
  
trackpan_amb33_t::trackpan_amb33_t(double srate, uint32_t fragsize, double maxdist_)
//  : delayline(3*srate,srate,340.0), 
  : delayline(maxdist_/340.0*srate+2,srate,340.0), 
    srate_(srate),
    dt_sample(1.0/srate_),
    dt_update(1.0/(double)fragsize),
    fragsize_(fragsize),
    y(0),
    dscale(srate/(340.0*7782.0)),
    maxdist(maxdist_)
{
  for(unsigned int k=0;k<idx::channels;k++)
    _w[k] = w_current[k] = dw[k] = 0;
  clp_current = dclp = 0;
}

trackpan_amb33_t::trackpan_amb33_t(const trackpan_amb33_t& src)
  : delayline(src.maxdist/340.0*src.srate_+2,src.srate_,340.0),
    srate_(src.srate_),
    dt_sample(1.0/srate_),
    dt_update(1.0/(double)(src.fragsize_)),
    fragsize_(src.fragsize_),
    y(0),
    dscale(src.srate_/(340.0*7782.0)),
    maxdist(src.maxdist)
{
  for(unsigned int k=0;k<idx::channels;k++)
    _w[k] = w_current[k] = dw[k] = 0;
  clp_current = dclp = 0;
}

trackpan_amb33_t::~trackpan_amb33_t()
{
  //DEBUG(data);
}

void trackpan_amb33_t::updatepar(pos_t src_)
{
  //DEBUG(src_.print_cart());
  float az = src_.azim();
  float el = src_.elev();
  float nm = src_.norm();
  //DEBUG(az);
  //DEBUG(el);
  float t, x2, y2, z2;
  dd = (nm-d_current)*dt_update;
  // this is taken from AMB plugins by Fons and Joern:
  _w[idx::w] = MIN3DB;
  t = cosf (el);
  _w[idx::x] = t * cosf (az);
  _w[idx::y] = t * sinf (az);
  _w[idx::z] = sinf (el);
  x2 = _w[idx::x] * _w[idx::x];
  y2 = _w[idx::y] * _w[idx::y];
  z2 = _w[idx::z] * _w[idx::z];
  _w[idx::u] = x2 - y2;
  _w[idx::v] = 2 * _w[idx::x] * _w[idx::y];
  _w[idx::s] = 2 * _w[idx::z] * _w[idx::x];
  _w[idx::t] = 2 * _w[idx::z] * _w[idx::y];
  _w[idx::r] = (3 * z2 - 1) / 2;
  _w[idx::p] = (x2 - 3 * y2) * _w[idx::x];
  _w[idx::q] = (3 * x2 - y2) * _w[idx::y];
  t = 2.598076f * _w[idx::z]; 
  _w[idx::n] = t * _w[idx::u];
  _w[idx::o] = t * _w[idx::v];
  t = 0.726184f * (5 * z2 - 1);
  _w[idx::l] = t * _w[idx::x];
  _w[idx::m] = t * _w[idx::y];
  _w[idx::k] = _w[idx::z] * (5 * z2 - 3) / 2;
  for(unsigned int k=0;k<idx::channels;k++)
    dw[k] = (_w[k] - w_current[k])*dt_update;
  dclp = (exp(-nm*dscale) - clp_current)*dt_update;
}

void trackpan_amb33_t::process(uint32_t n, float* vIn, const std::vector<float*>& outBuffer,uint32_t tp_frame, double tp_time,bool tp_rolling, sound_t* snd)
{
  if( n != fragsize_ )
    throw ErrMsg("fragsize changed");
  double ldt(dt_sample);
  if( !tp_rolling )
    ldt = 0;
  if( snd->isactive(tp_time) && vIn ){
    updatepar(snd->get_pos(tp_time+ldt*n));
    for( unsigned int i=0;i<n;i++){
      delayline.push(vIn[i]);
      float d = (d_current+=dd);
      float d1 = 1.0/std::max(1.0f,d);
      if( tp_rolling ){
        float c1(clp_current+=dclp);
        float c2(1.0f-c1);
        y = c2*y+c1*delayline.get_dist(d)*d1;
        for( unsigned int k=0;k<idx::channels;k++){
          outBuffer[k][i] += (w_current[k] += dw[k]) * y;
        }
      }
    }
  }
}

  
reverb_line_t::reverb_line_t(double srate, uint32_t fragsize, double maxdist_)
//  : delayline(3*srate,srate,340.0), 
  : delayline(maxdist_/340.0*srate+2,srate,340.0), 
    srate_(srate),
    dt_sample(1.0/srate_),
    dt_update(1.0/(double)fragsize),
    fragsize_(fragsize),
    d_global_current(0.0),
    d_border_current(0.0),
    clp_current(1.0),
    y(0),
    dscale(srate/(340.0*7782.0)),
    maxdist(maxdist_)
{
  clp_current = dclp = 0;
}

reverb_line_t::reverb_line_t(const reverb_line_t& src)
  : delayline(src.maxdist/340.0*src.srate_+2,src.srate_,340.0),
    srate_(src.srate_),
    dt_sample(1.0/srate_),
    dt_update(1.0/(double)(src.fragsize_)),
    fragsize_(src.fragsize_),
    d_global_current(0.0),
    d_border_current(0.0),
    clp_current(1.0),
    y(0),
    dscale(src.srate_/(340.0*7782.0)),
    maxdist(src.maxdist)
{
  clp_current = dclp = 0;
}

void reverb_line_t::process(uint32_t n, float* vIn, float* vOut,uint32_t tp_frame, double tp_time,bool tp_rolling, sound_t* snd, diffuse_reverb_t* reverb)
{
  if( n != fragsize_ )
    throw ErrMsg("fragsize changed");
  if( snd->isactive(tp_time) && vIn ){
    double ldt(dt_sample);
    if( !tp_rolling )
      ldt = 0;
    pos_t p(snd->get_pos_global(tp_time+ldt*n));
    double d_global(p.norm());
    double d_border(0.25*reverb->size.norm()+reverb->border_distance(p));
    //DEBUG(d_global);
    //DEBUG(d_border);
    dd_global = (d_global-d_global_current)*dt_update;
    dd_border = (d_border-d_border_current)*dt_update;
    dclp = (exp(-d_border*dscale) - clp_current)*dt_update;
    for( unsigned int i=0;i<n;i++){
      delayline.push(vIn[i]);
      d_global = (d_global_current+=dd_global);
      d_border = (d_border_current+=dd_border);
      float d1 = 1.0/std::max(1.0,d_border);
      float c1(clp_current+=dclp);
      float c2(1.0f-c1);
      if( tp_rolling ){
        y = c2*y+c1*delayline.get_dist(d_global)*d1;
        vOut[i] += y;
      }
    }
  }
}

namespace TASCAR {

  /**
     \ingroup apptascar
     \brief Multiple panning methods
  */
  class scene_generator_t : public jackc_transport_t, public scene_t, public osc_server_t {
  public:
    scene_generator_t(const std::string& name, const std::string& oscport);
    ~scene_generator_t();
    void run(const std::string& ambdec = "ambdec");
    void connect_all(const std::string& ambdec);
  private:
    std::vector<TASCAR::trackpan_amb33_t> panner;
    std::vector<std::vector<TASCAR::reverb_line_t> > rvbline;
    std::vector<TASCAR::sound_t*> sounds;
    std::vector<TASCAR::Input::jack_t*> jackinputs;
    std::vector<std::vector<TASCAR::mirror_pan_t> > mirror;
    // jack callback:
    int process(jack_nframes_t nframes,const std::vector<float*>& inBuffer,const std::vector<float*>& outBuffer, uint32_t tp_frame, bool tp_rolling);
    std::vector<std::string> vAmbPorts;
    static int osc_solo(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data);
    static int osc_mute(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data);
    static int osc_listener_orientation(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data);
    static int osc_listener_position(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data);
    uint32_t first_reverb_port;
  public:
  };

}

static bool b_quit;

TASCAR::scene_generator_t::scene_generator_t(const std::string& name, const std::string& oscport)
  : jackc_transport_t(name),osc_server_t("",oscport,true)
    
{
  char c_tmp[100];
  unsigned int k=0;
  for(int ko=0;ko<=3;ko++){
    for(int kl=-ko;kl<=ko;kl++){
      sprintf(c_tmp,"out.%d%c",ko,channelorder[k]);
      add_output_port(c_tmp);
      sprintf(c_tmp,"in.%d%c",ko,channelorder[k]);
      vAmbPorts.push_back(c_tmp);
      k++;
    }
  }
  osc_server_t::add_method("/solo","si",osc_solo,this);
  osc_server_t::add_method("/mute","si",osc_mute,this);
  osc_server_t::add_method("/listener/pos","fff",osc_listener_position,this);
  osc_server_t::add_method("/listener/rot","fff",osc_listener_orientation,this);
  osc_server_t::add_method("/listener/pos","ff",osc_listener_position,this);
  osc_server_t::add_method("/listener/rot","f",osc_listener_orientation,this);
}

TASCAR::scene_generator_t::~scene_generator_t()
{
}

int TASCAR::scene_generator_t::osc_solo(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data)
{
  TASCAR::scene_generator_t* h((TASCAR::scene_generator_t*)user_data);
  if( h && (argc == 2) && (types[0] == 's') && (types[1] == 'i') ){
    h->set_solo(&(argv[0]->s),argv[1]->i);
    return 0;
  }
  return 1;
}

int TASCAR::scene_generator_t::osc_mute(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data)
{
  TASCAR::scene_generator_t* h((TASCAR::scene_generator_t*)user_data);
  if( h && (argc == 2) && (types[0] == 's') && (types[1] == 'i') ){
    h->set_mute(&(argv[0]->s),argv[1]->i);
    return 0;
  }
  return 1;
}

int TASCAR::scene_generator_t::osc_listener_orientation(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data)
{
  TASCAR::scene_generator_t* h((TASCAR::scene_generator_t*)user_data);
  if( h && (argc == 3) && (types[0]=='f') && (types[1]=='f') && (types[2]=='f') ){
    zyx_euler_t r;
    r.z = DEG2RAD*argv[0]->f;
    r.y = DEG2RAD*argv[1]->f;
    r.x = DEG2RAD*argv[2]->f;
    h->listener_orientation(r);
    return 0;
  }
  if( h && (argc == 1) && (types[0]=='f') ){
    zyx_euler_t r;
    r.z = DEG2RAD*argv[0]->f;
    h->listener_orientation(r);
    return 0;
  }
  return 1;
}

int TASCAR::scene_generator_t::osc_listener_position(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data)
{
  TASCAR::scene_generator_t* h((TASCAR::scene_generator_t*)user_data);
  if( h && (argc == 3) && (types[0]=='f') && (types[1]=='f') && (types[2]=='f') ){
    pos_t r;
    r.x = argv[0]->f;
    r.y = argv[1]->f;
    r.z = argv[2]->f;
    h->listener_position(r);
    return 0;
  }
  if( h && (argc == 2) && (types[0]=='f') && (types[1]=='f') ){
    pos_t r;
    r.x = argv[0]->f;
    r.y = argv[1]->f;
    h->listener_position(r);
    return 0;
  }
  return 1;
}

void TASCAR::scene_generator_t::connect_all(const std::string& dest_name)
{
  for( unsigned int k=0;k<vAmbPorts.size();k++){
    try{
      connect_out( k, dest_name + ":" + vAmbPorts[k]);
    }
    catch( const std::exception& msg ){
      std::cerr << "Warning: " << msg.what() << std::endl;
    }
    catch( const char* msg ){
      std::cerr << "Warning: " << msg << std::endl;
    }
  }
  for( unsigned int k=0;k<reverbs.size();k++){
    try{
      connect_out( k+first_reverb_port, "zita-rev1:in.L" );
    }
    catch( const std::exception& msg ){
      std::cerr << "Warning: " << msg.what() << std::endl;
    }
    catch( const char* msg ){
      std::cerr << "Warning: " << msg << std::endl;
    }
  }
}
int TASCAR::scene_generator_t::process(jack_nframes_t nframes,
                                       const std::vector<float*>& inBuffer,
                                       const std::vector<float*>& outBuffer, 
                                       uint32_t tp_frame, bool tp_rolling)
{
  //DEBUG(anysolo);
  // mute output:
  for(unsigned int k=0;k<outBuffer.size();k++)
    memset(outBuffer[k],0,sizeof(float)*nframes);
  // play backgrounds:
  if( tp_rolling && (outBuffer.size()>=4) ){
    float* amb_buf[4];
    for(unsigned int k=0;k<4; k++)
      amb_buf[k] = outBuffer[k];
    for(std::vector<bg_amb_t>::iterator it=bg_amb.begin();it!=bg_amb.end();++it){
      if( !it->get_mute() && ((anysolo ==0)||(it->get_solo())))
        it->request_data( tp_frame, nframes, 4, amb_buf );
      //DEBUG(amb_buf[0][0]);
    }
  }
  for( unsigned int k=0;k<jackinputs.size();k++){
    jackinputs[k]->write(nframes,inBuffer[k]);
  }
  // load/update inputs:
  if( tp_rolling ){
    for(unsigned int k=0;k<srcobjects.size();k++){
      srcobjects[k].fill( tp_frame, nframes );
    }
  }else{
    for(unsigned int k=0;k<srcobjects.size();k++){
      srcobjects[k].fill( tp_frame, 0 );
    }
  }
  //DEBUG(panner.size());
  // process point sources:
  double tp_time((double)tp_frame/(double)srate);
  for( unsigned int k=0;k<panner.size();k++)
    if( sounds[k] && get_playsound(sounds[k]) )
      panner[k].process(nframes,sounds[k]->get_buffer(nframes), outBuffer,tp_frame,
                        tp_time,tp_rolling,sounds[k]);
  // process mirror sources:
  for( unsigned int k_face=0;k_face<mirror.size();k_face++){
    if( (!faces[k_face].get_mute()) && faces[k_face].isactive(tp_time) ){
    //if( true ){
      for( unsigned int k_snd=0;k_snd<mirror[k_face].size();k_snd++){
        //if( sounds[k_snd] && get_playsound(sounds[k_snd]) ){
        if( sounds[k_snd] ){
          mirror[k_face][k_snd].process(nframes,sounds[k_snd]->get_buffer(nframes),outBuffer,tp_frame,tp_time,tp_rolling,sounds[k_snd],&(faces[k_face]));
        }
      }
    }
  }
  // process diffuse reverb:
  for( unsigned int kr=0;kr<reverbs.size();kr++){
    memset(outBuffer[kr+first_reverb_port],0,nframes*sizeof(float));
    if( !reverbs[kr].get_mute() )
      for( unsigned int k=0;k<rvbline[kr].size();k++)
        if( sounds[k] && get_playsound(sounds[k]) )
          rvbline[kr][k].process(nframes,sounds[k]->get_buffer(nframes),outBuffer[first_reverb_port+kr],tp_frame,tp_time,tp_rolling,sounds[k],&reverbs[kr]);
    
  }
  if( (duration <= tp_time) && (duration + (double)nframes/(double)srate > tp_time) )
    tp_stop();
  return 0;
}

void TASCAR::scene_generator_t::run(const std::string& dest_ambdec)
{
  sounds = linearize_sounds();
  prepare(get_srate(), get_fragsize());
  panner.clear();
  rvbline.resize(reverbs.size());
  for(unsigned int kr=0;kr<rvbline.size();kr++)
    rvbline[kr].clear();
  mirror.resize(faces.size());
  for( unsigned int k_face=0;k_face<mirror.size();k_face++)
    mirror[k_face].clear();
  for(unsigned int k=0;k<sounds.size();k++){
    double maxdist(0);
    for( double t=0;t<duration;t+=2){
      pos_t p(sounds[k]->get_pos(t));
      maxdist = std::max(maxdist,p.norm());
    }
    std::cout << sounds[k]->getlabel() << " maxdist=" << maxdist << std::endl;
    panner.push_back(trackpan_amb33_t(get_srate(), get_fragsize(), maxdist+100));
    for(unsigned int kr=0;kr<rvbline.size();kr++)
      rvbline[kr].push_back(reverb_line_t(get_srate(), get_fragsize(), maxdist+100));
    for( unsigned int k_face=0;k_face<mirror.size();k_face++)
      mirror[k_face].push_back(mirror_pan_t(get_srate(), get_fragsize(), maxdist+100));
  }
  for(unsigned int k=0;k<srcobjects.size();k++){
    srcobjects[k].fill( -1, 0 );
  }
  first_reverb_port = get_num_output_ports();
  for(unsigned int kr=0;kr<reverbs.size();kr++){
    add_output_port(reverbs[kr].get_name());
  }
  jackinputs = get_jack_inputs();
  for(unsigned int k=0;k<jackinputs.size();k++){
    add_input_port(jackinputs[k]->get_port_name());
  }
  //DEBUG(mirror.size());
  jackc_t::activate();
  osc_server_t::activate();
  if( dest_ambdec.size() ){
    connect_all( dest_ambdec );
  }
  while( !b_quit ){
    sleep( 1 );
    getchar();
    if( feof( stdin ) )
      b_quit = true;
    for(std::vector<bg_amb_t>::iterator it=bg_amb.begin();it!=bg_amb.end();++it){
      unsigned int xr(it->get_xruns());
      if( xr ){
        std::cerr << "xrun " << it->filename << " " << xr << std::endl;
      }
    }
  }
  osc_server_t::deactivate();
  jackc_t::deactivate();
}

static void sighandler(int sig)
{
  b_quit = true;
}

void usage(struct option * opt)
{
  std::cout << "Usage:\n\ntascar_scene -c configfile [options]\n\nOptions:\n\n";
  while( opt->name ){
    std::cout << "  -" << (char)(opt->val) << " " << (opt->has_arg?"#":"") <<
      "\n  --" << opt->name << (opt->has_arg?"=#":"") << "\n\n";
    opt++;
  }
}

int main(int argc, char** argv)
{
  try{
    b_quit = false;
    signal(SIGABRT, &sighandler);
    signal(SIGTERM, &sighandler);
    signal(SIGINT, &sighandler);
    std::string cfgfile("");
    std::string jackname("tascar_scene");
    std::string oscport("9877");
    const char *options = "c:hn:p:";
    struct option long_options[] = { 
      { "config",   1, 0, 'c' },
      { "help",     0, 0, 'h' },
      { "jackname", 1, 0, 'n' },
      { "oscport",  1, 0, 'p' },
      { 0, 0, 0, 0 }
    };
    int opt(0);
    int option_index(0);
    while( (opt = getopt_long(argc, argv, options,
                              long_options, &option_index)) != -1){
      switch(opt){
      case 'c':
        cfgfile = optarg;
        break;
      case 'h':
        usage(long_options);
        return -1;
      case 'n':
        jackname = optarg;
        break;
      case 'p':
        oscport = optarg;
        break;
      }
    }
    if( cfgfile.size() == 0 ){
      usage(long_options);
      return -1;
    }
    TASCAR::scene_generator_t S(jackname,oscport);
    S.read_xml(cfgfile);
    S.run();
  }
  catch( const std::exception& msg ){
    std::cerr << "Error: " << msg.what() << std::endl;
    return 1;
  }
  catch( const char* msg ){
    std::cerr << "Error: " << msg << std::endl;
    return 1;
  }
}

/*
 * Local Variables:
 * mode: c++
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * compile-command: "make -C .."
 * End:
 */
