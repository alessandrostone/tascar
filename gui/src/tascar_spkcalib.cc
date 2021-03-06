#include <iostream>
#include <stdint.h>
#include "tascar.h"
#include "spkcalib_glade.h"
#include <gtkmm.h>
#include <gtkmm/main.h>
#include <gtkmm/window.h>
#include <ctime>
#include "jackiowav.h"
#include "gui_elements.h"

#include "session.h"

using namespace TASCAR;
using namespace TASCAR::Scene;

class calibsession_t : public TASCAR::session_t
{
public:
  calibsession_t( const std::string& fname, double reflevel, const std::vector<std::string>& refport, double duration_ , double fmin, double fmax );
  ~calibsession_t();
  double get_caliblevel() const;
  double get_diffusegain() const;
  void inc_caliblevel(double dl);
  void inc_diffusegain(double dl);
  void set_active(bool b);
  void set_active_diff(bool b);
  void get_levels(Gtk::ProgressBar* rec_progress, double prewait);
  void reset_levels();
  void saveas( const std::string& fname );
  void save();
  bool complete() const { return levelsrecorded && calibrated && calibrated_diff; };
  bool modified() const { return levelsrecorded || calibrated || calibrated_diff || gainmodified; };
  std::string name() const {return spkname;};
  double get_lmin() const {return lmin;};
  double get_lmax() const {return lmax;};
  double get_lmean() const {return lmean;};
  bool levels_complete() const { return levelsrecorded; };
private:
  bool gainmodified;
  bool levelsrecorded;
  bool calibrated;
  bool calibrated_diff;
  //std::vector<std::string> refport;
  double startlevel;
  double startdiffgain;
  double delta;
  double delta_diff;
  std::string spkname;
  spk_array_t* spkarray;
  std::vector<double> levels;
  std::vector<std::string> refport_;
  double duration;
  double lmin;
  double lmax;
  double lmean;
};

calibsession_t::calibsession_t( const std::string& fname, double reflevel, const std::vector<std::string>& refport, double duration_ , double fmin, double fmax  )
  : session_t("<?xml version=\"1.0\"?><session srv_port=\"none\"/>",LOAD_STRING,""),
    gainmodified(false),
    levelsrecorded(false),
    calibrated(false),
    calibrated_diff(false),
    startlevel(0),
    startdiffgain(0),
    delta(0),
    delta_diff(0),
    spkname(fname),
    spkarray(NULL),
    refport_(refport),
    duration(duration_),
    lmin(0),
    lmax(0),
    lmean(0)
{
  doc->get_root_node()->set_attribute("srv_port","none");
  xmlpp::Element* e_scene(doc->get_root_node()->add_child("scene"));
  e_scene->set_attribute("name","calib");
  xmlpp::Element* e_src(e_scene->add_child("source"));
  e_src->set_attribute("mute","true");
  xmlpp::Element* e_snd(e_src->add_child("sound"));
  //e_snd->set_attribute("x","1");
  xmlpp::Element* e_plugs(e_snd->add_child("plugins"));
  xmlpp::Element* e_pink(e_plugs->add_child("pink"));
  e_pink->set_attribute("level",TASCAR::to_string(reflevel));
  e_pink->set_attribute("period",TASCAR::to_string(duration));
  e_pink->set_attribute("fmin",TASCAR::to_string(fmin));
  e_pink->set_attribute("fmax",TASCAR::to_string(fmax));
  xmlpp::Element* e_rcvr(e_scene->add_child("receiver"));
  e_rcvr->set_attribute("type","nsp");
  e_rcvr->set_attribute("layout",fname);
  xmlpp::Element* e_diff(e_scene->add_child("diffuse"));
  e_diff->set_attribute("mute","true");
  xmlpp::Element* e_plugs_diff(e_diff->add_child("plugins"));
  xmlpp::Element* e_pink_diff(e_plugs_diff->add_child("pink"));
  e_pink_diff->set_attribute("level",TASCAR::to_string(reflevel));
  e_pink_diff->set_attribute("period",TASCAR::to_string(duration));
  e_pink_diff->set_attribute("fmin",TASCAR::to_string(fmin));
  e_pink_diff->set_attribute("fmax",TASCAR::to_string(fmax));
  //doc->write_to_file_formatted("temp.cfg");
  add_scene(e_scene);
  startlevel = get_caliblevel();
  startdiffgain = get_diffusegain();
  spkarray = new spk_array_t( e_rcvr, false );
  levels = std::vector<double>(spkarray->size(),0.0);
  if( !scenes.empty() )
    if( !scenes.back()->source_objects.empty() ){
      scenes.back()->source_objects.back()->dlocation = pos_t(1,0,0);
    }
}

calibsession_t::~calibsession_t()
{
  delete spkarray;
}

void calibsession_t::reset_levels()
{
  levelsrecorded = false;
  if( !scenes.empty() ){
    if( !scenes.back()->receivermod_objects.empty() ){
      TASCAR::receivermod_base_speaker_t* recspk(dynamic_cast<TASCAR::receivermod_base_speaker_t*>(scenes.back()->receivermod_objects.back()->libdata));
      if( recspk ){
        for(uint32_t k=0;k<levels.size();++k)
          recspk->spkpos[k].gain = 1.0;
      }
    }
  }
}

void calibsession_t::get_levels(Gtk::ProgressBar* rec_progress, double prewait)
{
  if( !scenes.empty() )
    if( !scenes.back()->source_objects.empty() ){
      levels.clear();
      scenes.back()->source_objects.back()->set_mute(false);
      uint32_t frac(0);
      rec_progress->set_fraction(0.0);
      while(Gtk::Main::events_pending())
        Gtk::Main::iteration(false);
      for( auto spk=spkarray->begin(); spk!=spkarray->end(); ++spk ){
        scenes.back()->source_objects.back()->dlocation = spk->unitvector;
        usleep(1e6*prewait);
        jackio_t rec( duration, "", refport_ );
        rec.run();
        wave_t brec( rec.nframes_total*refport_.size() );
        for( uint32_t ch=0;ch<refport_.size();++ch)
          for( uint32_t k=0;k<rec.nframes_total;++k )
            brec[ch*rec.nframes_total+k] = rec.buf_out[k*refport_.size()+ch];
        TASCAR::levelmeter_t levelmeter( rec.get_srate(), duration, TASCAR::levelmeter::C );
        levelmeter.update( brec );
        levels.push_back( 10*log10(levelmeter.ms()) );
        ++frac;
        rec_progress->set_fraction((double)frac/(double)(spkarray->size()));
        while(Gtk::Main::events_pending())
          Gtk::Main::iteration(false);
      }
      // convert levels into gains:
      lmin = levels[0];
      lmax = levels[0];
      lmean = 0;
      for( auto l=levels.begin();l!=levels.end();++l){
        lmean += *l;
        lmin = std::min(*l,lmin);
        lmax = std::max(*l,lmax);
      }
      lmean /= levels.size();
      if( !scenes.back()->receivermod_objects.empty() ){
        TASCAR::receivermod_base_speaker_t* recspk(dynamic_cast<TASCAR::receivermod_base_speaker_t*>(scenes.back()->receivermod_objects.back()->libdata));
        if( recspk ){
          for(uint32_t k=0;k<levels.size();++k)
            recspk->spkpos[k].gain *= pow(10.0,0.05*(lmin - levels[k]));
          double lmax(0);
          for(uint32_t k=0;k<levels.size();++k)
            lmax = std::max(lmax,recspk->spkpos[k].gain);
          for(uint32_t k=0;k<levels.size();++k)
            recspk->spkpos[k].gain /= lmax;
        }
      }
      scenes.back()->source_objects.back()->set_mute(true);
      scenes.back()->source_objects.back()->dlocation = pos_t(1,0,0);
    }
  levelsrecorded = true;
}

void calibsession_t::saveas( const std::string& fname )
{
  // convert levels into gains:
  std::vector<double> gains;
  double lmin(levels[0]);
  for( auto l=levels.begin();l!=levels.end();++l)
    lmin = std::min(*l,lmin);
  for(uint32_t k=0;k<levels.size();++k)
    gains.push_back(20*log10((*spkarray)[k].gain) + lmin - levels[k]);
  // rewrite file:
  TASCAR::xml_doc_t doc(spkname, TASCAR::xml_doc_t::LOAD_FILE );
  if( doc.doc->get_root_node()->get_name() != "layout" )
    throw TASCAR::ErrMsg("Invalid file type, expected root node type \"layout\", got \""+doc.doc->get_root_node()->get_name()+"\".");
  TASCAR::xml_element_t elem(doc.doc->get_root_node());
  doc.doc->get_root_node()->set_attribute("caliblevel",TASCAR::to_string(get_caliblevel()));
  doc.doc->get_root_node()->set_attribute("diffusegain",TASCAR::to_string(get_diffusegain()));
  // update gains:
  if( !scenes.back()->receivermod_objects.empty() ){
    TASCAR::receivermod_base_speaker_t* recspk(dynamic_cast<TASCAR::receivermod_base_speaker_t*>(scenes.back()->receivermod_objects.back()->libdata));
    if( recspk ){
      //for(uint32_t k=0;k<levels.size();++k)
      //  recspk->spkpos[k].gain *= pow(10.0,0.05*(lmin - levels[k]));
      //
      //
      TASCAR::spk_array_t array(doc.doc->get_root_node(),true);
      xmlpp::Node::NodeList subnodes(doc.doc->get_root_node()->get_children());
      size_t k=0;
      for( auto sn=subnodes.begin(); sn!=subnodes.end(); ++sn ){
        xmlpp::Element* sne(dynamic_cast<xmlpp::Element*>(*sn));
        if( sne && ( sne->get_name() == "speaker" )){
          sne->set_attribute("gain",
                             TASCAR::to_string(20*log10(recspk->spkpos[std::min(k,recspk->spkpos.size()-1)].gain)));
          ++k;
        }
      }
    }
  }
  std::vector<std::string> attributes;
  attributes.push_back("decorr_length");
  attributes.push_back("decorr");
  attributes.push_back("densitycorr");
  attributes.push_back("caliblevel");
  attributes.push_back("diffusegain");
  attributes.push_back("gain");
  attributes.push_back("az");
  attributes.push_back("el");
  attributes.push_back("r");
  attributes.push_back("delay");
  attributes.push_back("compB");
  attributes.push_back("connect");
  size_t checksum(elem.hash(attributes, true));
  elem.set_attribute("checksum",checksum);
  char ctmp[1024];
  memset(ctmp,0,1024);
  std::time_t t(std::time(nullptr));
  std::strftime(ctmp,1023,"%Y-%m-%d %T",std::localtime(&t));
  doc.doc->get_root_node()->set_attribute("calibdate",ctmp);
  doc.doc->write_to_file_formatted(fname);
  gainmodified = false;
  levelsrecorded = false;
  calibrated = false;
  calibrated_diff = false;
}

void calibsession_t::save()
{
  saveas(spkname);
}

void calibsession_t::set_active(bool b)
{
  if( !scenes.empty() )
    if( !scenes.back()->source_objects.empty() ){
      if( b )
        set_active_diff( false );
      scenes.back()->source_objects.back()->dlocation = pos_t(1,0,0);
      scenes.back()->source_objects.back()->set_mute(!b);
      if( b )
        calibrated = true;
    }
}

void calibsession_t::set_active_diff(bool b)
{
  if( !scenes.empty() )
    if( !scenes.back()->diff_snd_field_objects.empty() ){
      if( b )
        set_active( false );
      scenes.back()->diff_snd_field_objects.back()->set_mute(!b);
      if( b )
        calibrated_diff = true;
    }
}

double calibsession_t::get_caliblevel() const
{
  if( !scenes.empty() )
    if( !scenes.back()->receivermod_objects.empty() )
      return 20*log10(scenes.back()->receivermod_objects.back()->caliblevel);
  return 20*log10(2e5);
}

double calibsession_t::get_diffusegain() const
{
  if( !scenes.empty() )
    if( !scenes.back()->receivermod_objects.empty() )
      return 20*log10(scenes.back()->receivermod_objects.back()->diffusegain);
  return 0;
}

void calibsession_t::inc_caliblevel(double dl)
{
  gainmodified = true;
  delta += dl;
  double gain(pow(10.0,0.05*(startlevel+delta)));
  if( !scenes.empty() )
    if( !scenes.back()->receivermod_objects.empty() )
      scenes.back()->receivermod_objects.back()->caliblevel = gain;
}

void calibsession_t::inc_diffusegain(double dl)
{
  gainmodified = true;
  delta_diff += dl;
  double gain(pow(10.0,0.05*(startdiffgain+delta_diff)));
  if( !scenes.empty() )
    if( !scenes.back()->receivermod_objects.empty() )
      scenes.back()->receivermod_objects.back()->diffusegain = gain;
}

class spkcalib_t : public Gtk::Window, public jackc_t
{
public:
  spkcalib_t(BaseObjectType* cobject, const Glib::RefPtr<Gtk::Builder>& refGlade);
  virtual ~spkcalib_t();
  void load(const std::string& fname);
  void cleanup();
  void levelinc(double d);
  void inc_diffusegain(double d);
  void update_display();
  virtual int process(jack_nframes_t nframes,const std::vector<float*>& inBuffer,const std::vector<float*>& outBuffer);
private:
  void manage_act_grp_save();
protected:
  Glib::RefPtr<Gtk::Builder> m_refBuilder;
  //Signal handlers:
  void on_reclevels();
  void on_resetlevels();
  void on_play();
  void on_stop();
  void on_dec_10();
  void on_dec_2();
  void on_dec_05();
  void on_inc_05();
  void on_inc_2();
  void on_inc_10();
  void on_play_diff();
  void on_stop_diff();
  void on_dec_diff_10();
  void on_dec_diff_2();
  void on_dec_diff_05();
  void on_inc_diff_05();
  void on_inc_diff_2();
  void on_inc_diff_10();
  void on_open();
  void on_close();
  void on_save();
  void on_saveas();
  void on_quit();
  void on_level_entered();
  void on_level_diff_entered();
  bool on_timeout();
  sigc::connection con_timeout;
  Glib::RefPtr<Gio::SimpleActionGroup> refActionGroupMain;
  Glib::RefPtr<Gio::SimpleActionGroup> refActionGroupSave;
  Glib::RefPtr<Gio::SimpleActionGroup> refActionGroupClose;
  Glib::RefPtr<Gio::SimpleActionGroup> refActionGroupCalib;
  Gtk::Label* label_levels;
  Gtk::Label* label_gains;
  Gtk::Label* text_levelresults;
  Gtk::Label* text_instruction;
  Gtk::Label* text_instruction_diff;
  Gtk::Label* text_caliblevel;
  Gtk::Entry* levelentry;
  Gtk::Entry* levelentry_diff;
  Gtk::ProgressBar* rec_progress;
  Gtk::Box* box_h;
  calibsession_t* session;
  double reflevel;
  double noiseperiod;
  double fmin;
  double fmax;
  double prewait;
  std::vector<std::string> refport;
  std::vector<TASCAR::levelmeter_t*> rmsmeter;
  std::vector<TASCAR::wave_t*> inwave;
  std::vector<TSCGUI::splmeter_t*> guimeter;
  double miccalibdb;
  double miccalib;
};

int spkcalib_t::process(jack_nframes_t nframes,const std::vector<float*>& inBuffer,const std::vector<float*>& outBuffer)
{
  for(uint32_t k=0;k<std::min(inBuffer.size(),rmsmeter.size());++k){
    if( rmsmeter[k] ){
      inwave[k]->copy(inBuffer[k],nframes,miccalib);
      rmsmeter[k]->update(*(inwave[k]));
    }
  }
  return 0;
}

bool spkcalib_t::on_timeout()
{
  for( uint32_t k=0;k<guimeter.size();++k){
    guimeter[k]->update_levelmeter( *(rmsmeter[k]), reflevel );
    guimeter[k]->invalidate_win();
  }
  return true;
}

#define GET_WIDGET(x) m_refBuilder->get_widget(#x,x);if( !x ) throw TASCAR::ErrMsg(std::string("No widget \"")+ #x + std::string("\" in builder."))

void error_message(const std::string& msg)
{
  std::cerr << "Error: " << msg << std::endl;
  Gtk::MessageDialog dialog("Error",false,Gtk::MESSAGE_ERROR);
  dialog.set_secondary_text(msg);
  dialog.run();
}

spkcalib_t::spkcalib_t(BaseObjectType* cobject, const Glib::RefPtr<Gtk::Builder>& refGlade)
  : Gtk::Window(cobject),
  jackc_t("tascar_spkcalib_levels"),
    m_refBuilder(refGlade),
    text_instruction(NULL),
    text_instruction_diff(NULL),
    text_caliblevel(NULL),
    session(NULL),
    reflevel(TASCAR::config("tascar.spkcalib.reflevel",80.0)),
    noiseperiod(TASCAR::config("tascar.spkcalib.noiseperiod",2.0)),
  fmin(TASCAR::config("tascar.spkcalib.fmin",62.5)),
  fmax(TASCAR::config("tascar.spkcalib.fmax",4000.0)),
  prewait(TASCAR::config("tascar.spkcalib.prewait",0.5)),
  refport(str2vecstr(TASCAR::config("tascar.spkcalib.inputport","system:capture_1"))),
  miccalibdb(TASCAR::config("tascar.spkcalib.miccalib",0.0)),
  miccalib(pow(10.0,0.05*miccalibdb)*2e-5)
{
  for( uint32_t k=0;k<refport.size();++k){
    add_input_port(std::string("in.")+TASCAR::to_string(k+1));
    rmsmeter.push_back(new TASCAR::levelmeter_t(get_srate(),noiseperiod,TASCAR::levelmeter::C));
    inwave.push_back(new TASCAR::wave_t(get_fragsize()));
    guimeter.push_back(new TSCGUI::splmeter_t());
    guimeter.back()->set_mode( TSCGUI::dameter_t::rmspeak );
    //guimeter.back()->set_min_and_range( reflevel-20.0, 30.0 );
    guimeter.back()->set_min_and_range( miccalibdb-40.0, 40.0 );
  }
  jackc_t::activate();
  for( uint32_t k=0;k<refport.size();++k)
    connect_in(k,refport[k],true);
  //Create actions for menus and toolbars:
  refActionGroupMain = Gio::SimpleActionGroup::create();
  refActionGroupSave = Gio::SimpleActionGroup::create();
  refActionGroupClose = Gio::SimpleActionGroup::create();
  refActionGroupCalib = Gio::SimpleActionGroup::create();
  refActionGroupMain->add_action("open",sigc::mem_fun(*this, &spkcalib_t::on_open));
  refActionGroupSave->add_action("save",sigc::mem_fun(*this, &spkcalib_t::on_save));
  refActionGroupSave->add_action("saveas",sigc::mem_fun(*this, &spkcalib_t::on_saveas));
  refActionGroupClose->add_action("close",sigc::mem_fun(*this, &spkcalib_t::on_close));
  refActionGroupMain->add_action("quit",sigc::mem_fun(*this, &spkcalib_t::on_quit));
  insert_action_group("main",refActionGroupMain);
  refActionGroupCalib->add_action("reclevels",sigc::mem_fun(*this, &spkcalib_t::on_reclevels));
  refActionGroupCalib->add_action("resetlevels",sigc::mem_fun(*this, &spkcalib_t::on_resetlevels));
  refActionGroupCalib->add_action("play",sigc::mem_fun(*this, &spkcalib_t::on_play));
  refActionGroupCalib->add_action("stop",sigc::mem_fun(*this, &spkcalib_t::on_stop));
  refActionGroupCalib->add_action("inc_10",sigc::mem_fun(*this, &spkcalib_t::on_inc_10));
  refActionGroupCalib->add_action("inc_2",sigc::mem_fun(*this, &spkcalib_t::on_inc_2));
  refActionGroupCalib->add_action("inc_05",sigc::mem_fun(*this, &spkcalib_t::on_inc_05));
  refActionGroupCalib->add_action("dec_10",sigc::mem_fun(*this, &spkcalib_t::on_dec_10));
  refActionGroupCalib->add_action("dec_2",sigc::mem_fun(*this, &spkcalib_t::on_dec_2));
  refActionGroupCalib->add_action("dec_05",sigc::mem_fun(*this, &spkcalib_t::on_dec_05));
  refActionGroupCalib->add_action("play_diff",sigc::mem_fun(*this, &spkcalib_t::on_play_diff));
  refActionGroupCalib->add_action("stop_diff",sigc::mem_fun(*this, &spkcalib_t::on_stop_diff));
  refActionGroupCalib->add_action("inc_diff_10",sigc::mem_fun(*this, &spkcalib_t::on_inc_diff_10));
  refActionGroupCalib->add_action("inc_diff_2",sigc::mem_fun(*this, &spkcalib_t::on_inc_diff_2));
  refActionGroupCalib->add_action("inc_diff_05",sigc::mem_fun(*this, &spkcalib_t::on_inc_diff_05));
  refActionGroupCalib->add_action("dec_diff_10",sigc::mem_fun(*this, &spkcalib_t::on_dec_diff_10));
  refActionGroupCalib->add_action("dec_diff_2",sigc::mem_fun(*this, &spkcalib_t::on_dec_diff_2));
  refActionGroupCalib->add_action("dec_diff_05",sigc::mem_fun(*this, &spkcalib_t::on_dec_diff_05));
  //insert_action_group("calib",refActionGroupCalib);
  GET_WIDGET(label_levels);
  GET_WIDGET(label_gains);
  GET_WIDGET(text_levelresults);
  GET_WIDGET(text_instruction_diff);
  GET_WIDGET(text_instruction);
  GET_WIDGET(text_caliblevel);
  GET_WIDGET(levelentry);
  GET_WIDGET(levelentry_diff);
  GET_WIDGET(rec_progress);
  GET_WIDGET(box_h);
  levelentry->signal_activate().connect(sigc::mem_fun(*this, &spkcalib_t::on_level_entered));
  levelentry_diff->signal_activate().connect(sigc::mem_fun(*this, &spkcalib_t::on_level_diff_entered));
  con_timeout = Glib::signal_timeout().connect( sigc::mem_fun(*this, &spkcalib_t::on_timeout), 250 );
  for( uint32_t k=0;k<guimeter.size();++k){
    box_h->add(*guimeter[k]);
  }
  update_display();
  show_all();
}

void spkcalib_t::on_level_entered()
{
  try{
    double newlevel(reflevel);
    std::string slevel(levelentry->get_text());
    newlevel = atof(slevel.c_str());
    levelinc(reflevel-newlevel);
  }
  catch( const std::exception& e ){
    error_message(e.what());
  }
}

void spkcalib_t::on_level_diff_entered()
{
  try{
    double newlevel(reflevel);
    std::string slevel(levelentry_diff->get_text());
    newlevel = atof(slevel.c_str());
    inc_diffusegain(reflevel-newlevel);
  }
  catch( const std::exception& e ){
    error_message(e.what());
  }
}

void spkcalib_t::manage_act_grp_save()
{
  if( session && session->complete() )
    insert_action_group("save",refActionGroupSave);
  else
    remove_action_group("save");
  if( session ){
    std::string gainstr;
    if( !session->scenes.back()->receivermod_objects.empty() ){
      TASCAR::receivermod_base_speaker_t* recspk(dynamic_cast<TASCAR::receivermod_base_speaker_t*>(session->scenes.back()->receivermod_objects.back()->libdata));
      if( recspk ){
        for(uint32_t k=0;k<recspk->spkpos.size();++k){
          char lc[1024];
          sprintf(lc,"%1.1f ",20*log10(recspk->spkpos[k].gain));
          gainstr += lc;
        }
      }
    }
    label_gains->set_text(gainstr);
  }else{
    label_gains->set_text("");
  }
  if( session && session->levels_complete() ){
    char ctmp[1024];
    sprintf(ctmp,"Mean level: %1.1f dB FS (range: %1.1f dB)",
            session->get_lmean(),
            session->get_lmax()-session->get_lmin());
    text_levelresults->set_text(ctmp);
  }else{
    text_levelresults->set_text("");
  }
  if( session ){
    std::string smodified("");
    if( session->modified() )
      smodified = " (modified)";
    set_title(std::string("TASCAR speaker calibration [") + 
              std::string(basename(session->name().c_str())) + 
              std::string("]")+smodified );
  }else
    set_title("TASCAR speaker calibration");
}

void spkcalib_t::on_reclevels()
{
  try{
    if( session ){
      remove_action_group("calib");
      remove_action_group("close");
      levelentry->set_sensitive(false);
      levelentry_diff->set_sensitive(false);
      session->get_levels(rec_progress, prewait);
      insert_action_group("calib",refActionGroupCalib);
      insert_action_group("close",refActionGroupClose);
      levelentry->set_sensitive(true);
      levelentry_diff->set_sensitive(true);
    }
    manage_act_grp_save();
  }
  catch( const std::exception& e ){
    error_message(e.what());
  }
}

void spkcalib_t::on_resetlevels()
{
  try{
    if( session )
      session->reset_levels();
    manage_act_grp_save();
  }
  catch( const std::exception& e ){
    error_message(e.what());
  }
}

void spkcalib_t::on_play()
{
  if( session )
    session->set_active(true);
  manage_act_grp_save();
}

void spkcalib_t::on_stop()
{
  if( session )
    session->set_active(false);
}

void spkcalib_t::on_dec_10()
{
  levelinc(-10);
}

void spkcalib_t::on_dec_2()
{
  levelinc(-2);
}

void spkcalib_t::on_dec_05()
{
  levelinc(-0.5);
}

void spkcalib_t::on_inc_05()
{
  levelinc(0.5);
}

void spkcalib_t::on_inc_2()
{
  levelinc(2);
}

void spkcalib_t::on_inc_10()
{
  levelinc(10);
}

void spkcalib_t::on_play_diff()
{
  if( session )
    session->set_active_diff(true);
  manage_act_grp_save();
}

void spkcalib_t::on_stop_diff()
{
  if( session )
    session->set_active_diff(false);
}

void spkcalib_t::on_dec_diff_10()
{
  inc_diffusegain(-10);
}

void spkcalib_t::on_dec_diff_2()
{
  inc_diffusegain(-2);
}

void spkcalib_t::on_dec_diff_05()
{
  inc_diffusegain(-0.5);
}

void spkcalib_t::on_inc_diff_05()
{
  inc_diffusegain(0.5);
}

void spkcalib_t::on_inc_diff_2()
{
  inc_diffusegain(2);
}

void spkcalib_t::on_inc_diff_10()
{
  inc_diffusegain(10);
}

void spkcalib_t::on_quit()
{
  hide(); //Closes the main window to stop the app->run().
}

void spkcalib_t::on_open()
{
  Gtk::FileChooserDialog dialog("Please choose a file",
                                Gtk::FILE_CHOOSER_ACTION_OPEN);
  dialog.set_transient_for(*this);
  //Add response buttons the the dialog:
  dialog.add_button(Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
  dialog.add_button(Gtk::Stock::OPEN, Gtk::RESPONSE_OK);
  //Add filters, so that only certain file types can be selected:
  Glib::RefPtr<Gtk::FileFilter> filter_tascar = Gtk::FileFilter::create();
  filter_tascar->set_name("speaker layout files");
  filter_tascar->add_pattern("*.spk");
  dialog.add_filter(filter_tascar);
  Glib::RefPtr<Gtk::FileFilter> filter_any = Gtk::FileFilter::create();
  filter_any->set_name("Any files");
  filter_any->add_pattern("*");
  dialog.add_filter(filter_any);
  //Show the dialog and wait for a user response:
  int result = dialog.run();
  //Handle the response:
  if( result == Gtk::RESPONSE_OK){
    //Notice that this is a std::string, not a Glib::ustring.
    std::string filename = dialog.get_filename();
    if( !filename.empty() ){
      try{
        warnings.clear();
        load(filename);
      }
      catch( const std::exception& e){
        error_message(e.what());
      }
    }
  }
}

void spkcalib_t::on_close()
{
  cleanup();
  update_display();
}

void spkcalib_t::levelinc(double d)
{
  if( session )
    session->inc_caliblevel(-d);
  update_display();
}

void spkcalib_t::inc_diffusegain(double d)
{
  if( session )
    session->inc_diffusegain(d);
  update_display();
}

void spkcalib_t::on_save()
{
  try{
    if( session )
      session->save();
    update_display();
  }
  catch( const std::exception& e){
    error_message(e.what());
  }
}

void spkcalib_t::on_saveas()
{
  if( session ){
    Gtk::FileChooserDialog dialog("Please choose a file",
                                  Gtk::FILE_CHOOSER_ACTION_SAVE);
    dialog.set_transient_for(*this);
    //Add response buttons the the dialog:
    dialog.add_button(Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
    dialog.add_button(Gtk::Stock::OPEN, Gtk::RESPONSE_OK);
    //Add filters, so that only certain file types can be selected:
    Glib::RefPtr<Gtk::FileFilter> filter_tascar = Gtk::FileFilter::create();
    filter_tascar->set_name("speaker layout files");
    filter_tascar->add_pattern("*.spk");
    dialog.add_filter(filter_tascar);
    Glib::RefPtr<Gtk::FileFilter> filter_any = Gtk::FileFilter::create();
    filter_any->set_name("Any files");
    filter_any->add_pattern("*");
    dialog.add_filter(filter_any);
    //Show the dialog and wait for a user response:
    int result = dialog.run();
    //Handle the response:
    if( result == Gtk::RESPONSE_OK){
      //Notice that this is a std::string, not a Glib::ustring.
      std::string filename = dialog.get_filename();
      try{
        warnings.clear();
        if( session )
          session->saveas(filename);
        load(filename);
        update_display();
      }
      catch( const std::exception& e){
        error_message(e.what());
      }
    }
  }
}

spkcalib_t::~spkcalib_t()
{
  con_timeout.disconnect();
  cleanup();
  for( auto it=rmsmeter.begin();it!=rmsmeter.end();++it)
    delete *it;
  for( auto it=inwave.begin();it!=inwave.end();++it)
    delete *it;
  for( auto it=guimeter.begin();it!=guimeter.end();++it)
    delete *it;
}

void spkcalib_t::update_display()
{
  rec_progress->set_fraction(0);
  if( session ){
    char ctmp[1024];
    sprintf(ctmp,"caliblevel: %1.1f dB diffusegain: %1.1f dB",
            session->get_caliblevel(),
            session->get_diffusegain());
    text_caliblevel->set_text(ctmp);
  }else{
    text_caliblevel->set_text("no layout file loaded.");
  }
  std::string portlist;
  for(auto it=refport.begin();it!=refport.end();++it)
    portlist += *it + " ";
  if( portlist.size() )
    portlist.erase(portlist.size()-1,1);
  char ctmp[1024];
  sprintf(ctmp,"1. Relative loudspeaker gains:\nPlace a measurement microphone at the listening position and connect to port%s \"%s\". A pink noise will be played from the loudspeaker positions. Press record to start.",(refport.size()>1)?"s":"",portlist.c_str());
  label_levels->set_text(ctmp);
  sprintf(ctmp,"2. Adjust the playback level to %1.1f dB using the inc/dec buttons. Alternatively, enter the measured level in the field below. Use Z or C weighting.\n a) Point source:",reflevel);
  text_instruction->set_text(ctmp);
  text_instruction_diff->set_text(" b) Diffuse sound field:");
  if( warnings.size() ){
    Gtk::MessageDialog dialog(*this,"Warning",false,Gtk::MESSAGE_WARNING);
    std::string msg;
    for(auto it=warnings.begin();it!=warnings.end();++it)
      msg += *it + "\n";
    dialog.set_secondary_text(msg);
    dialog.run();
    warnings.clear();
  }
  manage_act_grp_save();
  if( session ){
    insert_action_group("calib",refActionGroupCalib);
    insert_action_group("close",refActionGroupClose);
    levelentry->set_sensitive(true);
    levelentry_diff->set_sensitive(true);
  }else{
    remove_action_group("calib");
    remove_action_group("close");
    levelentry->set_text("");
    levelentry_diff->set_text("");
    levelentry->set_sensitive(false);
    levelentry_diff->set_sensitive(false);
  }
}

void spkcalib_t::load(const std::string& fname)
{
  cleanup();
  if( fname.empty() )
    throw TASCAR::ErrMsg("Empty file name.");
  session = new calibsession_t(fname,reflevel,refport,noiseperiod,fmin,fmax);
  session->start();
  update_display();
}

void spkcalib_t::cleanup()
{
  if( session ){
    session->stop();
    delete session;
    session = NULL;
  }
  update_display();
}

int main(int argc, char** argv)
{
  TASCAR::config.forceoverwrite("tascar.spkcalib.maxage","3650");
  setlocale(LC_ALL,"C");
  int nargv(1);
  Glib::RefPtr<Gtk::Application> app = Gtk::Application::create(nargv, argv, "de.hoertech.tascarspkcalib",Gio::APPLICATION_NON_UNIQUE);
  Glib::RefPtr<Gtk::Builder> refBuilder;
  spkcalib_t* win(NULL);
  refBuilder = Gtk::Builder::create();
  refBuilder->add_from_string(ui_spkcalib);
  refBuilder->get_widget_derived("MainWindow", win);
  if( !win )
    throw TASCAR::ErrMsg("No main window");
  win->show_all();
  if( argc > 1 ){
    try{
      win->load(argv[1]);
    }
    catch(const std::exception& e){
      std::string msg("While loading file \"");
      msg += argv[1];
      msg += "\": ";
      msg += e.what();
      std::cerr << "Error: " << msg << std::endl;
      error_message(msg);
    }
  }
  int rv(app->run(*win));
  delete win;
  return rv;
}

/*
 * Local Variables:
 * mode: c++
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * compile-command: "make -C .."
 * End:
 */
