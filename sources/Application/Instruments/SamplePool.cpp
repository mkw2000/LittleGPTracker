#include "SamplePool.h"
#include <string.h>
#include <stdlib.h>
#include "System/Console/Trace.h"
#include "Application/Persistency/PersistencyService.h" 
#include "System/io/Status.h"
#include <string>
#include "SoundFontSample.h"
#include "SoundFontPreset.h"
#include "SoundFontManager.h"
#include "Application/Model/Config.h"

#define SAMPLE_LIB "root:samplelib" 

SamplePool::SamplePool() {
	for (int i=0;i<MAX_PIG_SAMPLES;i++) {
		names_[i]=NULL ;
		wav_[i]=NULL ;
	} ;
	count_=0 ;
} ;

SamplePool::~SamplePool() {
	for (int i=0;i<MAX_PIG_SAMPLES;i++) {
		SAFE_DELETE(wav_[i]) ;
		SAFE_FREE(names_[i]) ;
	} ;
} ;

const char *SamplePool::GetSampleLib() {
	Config *config=Config::GetInstance() ;
	const char *lib=config->GetValue("SAMPLELIB") ;
	return lib?lib:SAMPLE_LIB ;
} 

void SamplePool::Reset() {
	count_=0 ;
	for (int i=0;i<MAX_PIG_SAMPLES;i++) {
		SAFE_DELETE(wav_[i]) ;
		SAFE_FREE(names_[i]) ;
	} ;
	SoundFontManager::GetInstance()->Reset() ;
} ;

/*
  Returns an element of
  {SLOAD_OK, SLOAD_ERR_INVALID_DIR, SLOAD_ERR_MAX_SAMPLES,
   SLOAD_ERR_MAX_SOUNDFONTS, SLOAD_ERR_MAX_SAMPLES | SLOAD_ERR_MAX_SOUNDFONTS}.
*/
unsigned int SamplePool::Load() {

    Path sampleDir("samples:");

    I_Dir *dir = FileSystem::GetInstance()->Open(sampleDir.GetPath().c_str());
    if (!dir) {
        return SLOAD_ERR_INVALID_DIR;
    }

    unsigned int result = SLOAD_OK;

    // First, find all wav files

    dir->GetContent("*.wav");
    IteratorPtr<Path> it(dir->GetIterator()) ;
	count_=0 ;

	for(it->Begin();!it->IsDone();it->Next()) {
		Path &path=it->CurrentItem() ;
        Trace::Log("Load", "%s", path.GetCanonicalPath().c_str());
        loadSample(path.GetPath().c_str()) ;
		if (count_==MAX_PIG_SAMPLES) {
            result |= SLOAD_ERR_MAX_SAMPLES;
            Trace::Error("Warning maximum sample count reached");
            break;
		} ;

	} ;

	// now, let's look at soundfonts

	dir->GetContent("*.sf2") ;
	IteratorPtr<Path> it2(dir->GetIterator()) ;
    int sf_idx = 0;

    for (it2->Begin(); !it2->IsDone(); it2->Next(), sf_idx++) {
        Path &path=it2->CurrentItem() ;
        Trace::Log("Load", "%s", path.GetCanonicalPath().c_str());
        loadSoundFont(path.GetPath().c_str()) ;
		if (sf_idx == MAX_SOUNDFONTS) {
            result |= SLOAD_ERR_MAX_SOUNDFONTS;
            Trace::Error("Warning maximum soundfont count reached");
            break;
		} ;
    };

    delete dir;

    // now sort the samples
    Sort();

    return result;
};

void SamplePool::Sort() {
    int rest=count_;
	while(rest>0) {
        int index = 0;
        for (int i=1;i<rest;i++) {
			if (strcmp(names_[i],names_[index])>0) {
                index = i;
            }
        }
        SoundSource *tWav = wav_[index];
		char *tName = names_[index];
		wav_[index] = wav_[rest-1];
		names_[index] = names_[rest-1];
		wav_[rest-1] = tWav;
        names_[rest - 1] = tName;
        rest--;
	}
}

int SamplePool::getIndexOf(const char *name) {
    for (int i=0;i<count_;i++) {
		if (strcmp(names_[i], name)==0) {
			return i;
		}
	}
	return -1;
}

SoundSource *SamplePool::GetSource(int i) {
	return wav_[i] ;
} ;

char **SamplePool::GetNameList() {
	return names_ ;
} ;

int SamplePool::GetNameListSize() {
	return count_ ;
} ;

/*
  Returns an element of
  {SLOAD_OK, SLOAD_ERR_MAX_SAMPLES, SLOAD_ERR_INPUT_FILE}.
*/
int SamplePool::loadSample(const char *path) {

    if (count_==MAX_PIG_SAMPLES) return SLOAD_ERR_MAX_SAMPLES ;

    Path wavPath(path);
    WavFile *wave = loadWavSource(path);
    if (!wave)
        return SLOAD_ERR_INPUT_FILE;

    const std::string name = wavPath.GetName();
    char *storedName = (char *)SYS_MALLOC(name.length() + 1);
    if (!storedName) {
        Trace::Error("Out of memory storing sample name %s", name.c_str());
        delete wave;
        return SLOAD_ERR_INPUT_FILE;
    }

    strcpy(storedName, name.c_str());
    wav_[count_] = wave;
    names_[count_] = storedName;
    count_++;
    return SLOAD_OK;
}

WavFile *SamplePool::loadWavSource(const char *path) {
    Path wavPath(path);
    Status::Set("Loading %s", wavPath.GetName().c_str());
    Trace::Log("loadSample", "%s", path);

    WavFile *wave = WavFile::Open(path);
    if (!wave) {
        Trace::Error("Failed to open sample %s", wavPath.GetName().c_str());
        return 0;
    }
    if (!wave->GetBuffer(0, wave->GetSize(-1))) {
        Trace::Error("Failed to load sample data %s",
                     wavPath.GetName().c_str());
        delete wave;
        return 0;
    }
    wave->Close();
    return wave;
}

/*
  Returns a nonnegative int or an element of
  {-SLOAD_ERR_INVALID_DIR, -SLOAD_ERR_INPUT_FILE, -SLOAD_ERR_MAX_SAMPLES,
   -SLOAD_ERR_MAX_SOUNDFONTS}.
*/
int SamplePool::ImportSample(Path &path) {

    // Importing a WAV that is already in this project's pool should simply
    // select the existing sample. The old path overwrote the project copy and
    // appended a duplicate name, which could fail on PSP or leave two entries
    // backed by different in-memory data.
    if (path.Matches("*.wav")) {
        int existingIndex = getIndexOf(path.GetName().c_str());
        if (existingIndex >= 0) {
            Trace::Log("ImportSample", "Reusing imported sample %s",
                       path.GetName().c_str());
            return existingIndex;
        }
    }

    if (count_ == MAX_PIG_SAMPLES)
        return -SLOAD_ERR_MAX_SAMPLES;

    // construct target path

    std::string dpath = "samples:";
    dpath+=path.GetName() ;
	Path dstPath(dpath.c_str()) ;

	if (path.GetCanonicalPath() == dstPath.GetCanonicalPath()) {
        Trace::Error("Cannot import a sample over itself");
        return -SLOAD_ERR_OUTPUT_FILE;
    }

    FileSystemService fileService;
    if (fileService.Copy(path, dstPath) < 0)
        return -SLOAD_ERR_OUTPUT_FILE;

    // now load the sample
    int status = dstPath.Matches("*.wav")
                     ? loadSample(dstPath.GetPath().c_str())
                     : loadSoundFont(dstPath.GetPath().c_str());

    if (status != SLOAD_OK) {
        FileSystem::GetInstance()->Delete(dstPath.GetPath().c_str());
        return -status;
    }

    SetChanged();
    SamplePoolEvent ev ;
	ev.index_=count_-1 ;
	ev.type_=SPET_INSERT ;
    NotifyObservers(&ev);
    return count_-1 ;
};

bool SamplePool::IsImported(std::string name) {
    std::string dpath="samples:";
    dpath += name;
    Path dstPath(dpath.c_str());
    Path checkPath(dstPath.GetPath());
    return checkPath.Exists();
}

/*
    Unsorted reassign for now
    Returns the index of the sample in the pool
    count_-1 position if new
    previous position if already imported
*/
int SamplePool::Reassign(std::string name) {
    int insertedIndex = getIndexOf(name.c_str());

    std::string aliasPath = "samples:";
    aliasPath += name;
    Path dstPath(aliasPath.c_str());
    WavFile *wave = loadWavSource(dstPath.GetCanonicalPath().c_str());
    if (!wave)
        return -1;

    if (insertedIndex >= 0) {
        SoundSource *oldWave = wav_[insertedIndex];
        wav_[insertedIndex] = wave;
        SAFE_DELETE(oldWave);
        SetChanged();
        SamplePoolEvent ev;
        ev.index_ = insertedIndex;
        ev.type_=SPET_REPLACE;
        NotifyObservers(&ev);
        return insertedIndex;
    }

    if (count_ == MAX_PIG_SAMPLES) {
        delete wave;
        return -1;
    }

    char *storedName = (char *)SYS_MALLOC(name.length() + 1);
    if (!storedName) {
        delete wave;
        return -1;
    }
    strcpy(storedName, name.c_str());
    wav_[count_] = wave;
    names_[count_] = storedName;
    int newIndex = count_++;

    SetChanged();
    SamplePoolEvent ev;
    ev.index_ = newIndex;
    ev.type_=SPET_INSERT;
    NotifyObservers(&ev);
    return newIndex;
}

void SamplePool::PurgeSample(int i) {
	if (i < 0 || i >= count_)
        return;

	// construct the path of the sample to delete

	std::string wavPath="samples:" ;
	wavPath+=names_[i] ;
	Path path(wavPath.c_str()) ;
	//delete wav
	SAFE_DELETE(wav_[i]) ;
	// delete name entry
	SAFE_FREE(names_[i]) ;

	// delete file
	FileSystem::GetInstance()->Delete(path.GetPath().c_str()) ;

	// shift all entries from deleted to end
	for (int j=i;j<count_-1;j++) {
		wav_[j]=wav_[j+1] ;
		names_[j]=names_[j+1] ;
	} ;
	// decrease sample count
	count_-- ;
	wav_[count_]=0 ;
	names_[count_]=0 ;

	// now notify observers
	SetChanged() ;
	SamplePoolEvent ev ;
	ev.index_=i ;
	ev.type_=SPET_DELETE ;
	NotifyObservers(&ev) ;
} ;

/*
  Returns an element of
  {SLOAD_OK, SLOAD_ERR_MAX_SOUNDFONTS, SLOAD_ERR_INPUT_FILE}.
*/
int SamplePool::loadSoundFont(const char *path) {

    Path sPath(path);
    Status::Set("Loading %s", sPath.GetName().c_str());
    Trace::Log("loadSoundFont", "%s", path);

    sfBankID id = SoundFontManager::GetInstance()->LoadBank(path);
    if (id==-SF_BANK_TABLE_FULL) {
		return SLOAD_ERR_MAX_SOUNDFONTS ;
    } else if (id < 0) {
        return SLOAD_ERR_INPUT_FILE;
    }

    // Grab the sample offset

    long offset = sfGetSMPLOffset(id);

    // Add all presets of the sf

    WORD presetCount = 0;
    SFPRESETHDRPTR pHeaders=sfGetPresetHdrs(id,&presetCount); 

	for (int i=0;i<presetCount;i++) {
		if (count_<MAX_PIG_SAMPLES) {
			sfPresetHdr current=pHeaders[i] ;
			wav_[count_]=new SoundFontPreset(id,i) ;
			const char *name=pHeaders[i].achPresetName ;
            Trace::Log("loadSoundFont", "%s", name);
            names_[count_] = (char *)SYS_MALLOC(strlen(name) + 1);
            strcpy(names_[count_], name);
            count_++;
		}
	}
    /*
        // Get Sample information

        WORD headerCount=0 ;
        SFSAMPLEHDRPTR  &headers=sfGetSampHdrs(id,&headerCount );

        // Loop on every sample, add them

        for (int i=0;i<headerCount;i++) {
            if (count_<MAX_PIG_SAMPLES) {
                sfSampleHdr &current=headers[i] ;
                wav_[count_]=new SoundFontSample(current) ;
                const char *name=headers[i].achSampleName ;
                names_[count_]=(char*)SYS_MALLOC(strlen(name)+1) ;
                strcpy(names_[count_],name) ;
                count_++ ;
            }
        }
    */
    return SLOAD_OK;
} ;
