/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */

#include "config.h"
#include "bulk_extractor_i.h"

#ifdef USE_HISTOGRAMS
#include "histogram.h"
#endif

#include "dfxml/src/hash_t.h"

/****************************************************************
 *** feature_recorder_set
 *** No mutex is needed for the feature_recorder_set because it is never
 *** modified after it is created, only the contained feature_recorders are modified.
 ****************************************************************/

const string feature_recorder_set::ALERT_RECORDER_NAME = "alerts";
const string feature_recorder_set::DISABLED_RECORDER_NAME = "disabled";

/* Create an empty recorder */
feature_recorder_set::feature_recorder_set(uint32_t flags_):flags(flags_),input_fname(),
                                                            outdir(),frm(),Mlock(),seen_set(),
                                                            seen_set_lock(),scanner_stats()
{
    if(flags & SET_DISABLED){
        create_name(DISABLED_RECORDER_NAME,false);
        frm[DISABLED_RECORDER_NAME]->set_flag(feature_recorder::FLAG_DISABLED);
        return;
    }
    assert(0);                          // need to have a disabled recorder
}

/**
 * Create a properly functioning feature recorder set.
 * If disabled, create a disabled feature_recorder that can respond to functions as requested.
 */
feature_recorder_set::feature_recorder_set(const feature_file_names_t &feature_files,
                                           const std::string &input_fname_,
                                           const std::string &outdir_,
                                           bool create_stop_files):
    flags(0),input_fname(input_fname_),outdir(outdir_),frm(),Mlock(),
    seen_set(),seen_set_lock(),scanner_stats()
{
    if(flags & SET_DISABLED){
        create_name(DISABLED_RECORDER_NAME,false);
        frm[DISABLED_RECORDER_NAME]->set_flag(feature_recorder::FLAG_DISABLED);
        return;
    }

    create_name(feature_recorder_set::ALERT_RECORDER_NAME,false); // make the alert recorder

    /* Create the requested feature files */
    for(set<string>::const_iterator it=feature_files.begin();it!=feature_files.end();it++){
        create_name(*it,create_stop_files);
    }
}

void feature_recorder_set::flush_all()
{
    for(feature_recorder_map::iterator i = frm.begin();i!=frm.end();i++){
        i->second->flush();
    } 
}

void feature_recorder_set::close_all()
{
    for(feature_recorder_map::iterator i = frm.begin();i!=frm.end();i++){
        i->second->close();
    } 
}


bool feature_recorder_set::has_name(string name) const
{
    return frm.find(name) != frm.end();
}

/*
 * Gets a feature_recorder_set.
 */
feature_recorder *feature_recorder_set::get_name(const std::string &name) 
{
    const std::string *thename = &name;
    if(flags & SET_DISABLED){           // if feature recorder set is disabled, return the disabled recorder.
        thename = &feature_recorder_set::DISABLED_RECORDER_NAME;
    }

    if(flags & ONLY_ALERT){
        thename = &feature_recorder_set::ALERT_RECORDER_NAME;
    }

    cppmutex::lock lock(Mlock);
    feature_recorder_map::const_iterator it = frm.find(*thename);
    if(it!=frm.end()) return it->second;
    return(0);                          // feature recorder does not exist
}


void feature_recorder_set::add_stats(string bucket,double seconds)
{
    cppmutex::lock lock(Mlock);
    class pstats &p = scanner_stats[bucket]; // get the location of the stats
    p.seconds += seconds;
    p.calls ++;
}

void feature_recorder_set::get_stats(void *user,stat_callback_t stat_callback)
{
    for(scanner_stats_map::const_iterator it = scanner_stats.begin();it!=scanner_stats.end();it++){
        (*stat_callback)(user,(*it).first,(*it).second.calls,(*it).second.seconds);
    }
}

void feature_recorder_set::create_name(string name,bool create_stop_file) 
{
    if(frm.find(name)!=frm.end()){
        std::cerr << "create_name: feature recorder '" << name << "' already exists\n";
        return;
    }

    feature_recorder *fr = new feature_recorder(outdir,input_fname,name);
    frm[name] = fr;
    if(create_stop_file){
        string name_stopped = name+"_stopped";
        
        fr->stop_list_recorder = new feature_recorder(outdir,input_fname,name_stopped);
        frm[name_stopped] = fr->stop_list_recorder;
    }
    
    if(flags & SET_DISABLED) return;        // don't open if we are disabled
    
    /* Open the output!*/
    fr->open();
    if(fr->stop_list_recorder) fr->stop_list_recorder->open();
}

feature_recorder *feature_recorder_set::get_alert_recorder()
{
    return get_name(feature_recorder_set::ALERT_RECORDER_NAME);
}


bool feature_recorder_set::check_previously_processed(const uint8_t *buf,size_t bufsize)
{
    std::string hexhash = md5_generator::hash_buf(buf,bufsize).hexdigest();

    cppmutex::lock lock(seen_set_lock);
    if(seen_set.find(hexhash) == seen_set.end()){
        seen_set.insert(hexhash);       // haven't seen it before; insert it
        return false;
    }
    return true;                        // I've seen it before
}
