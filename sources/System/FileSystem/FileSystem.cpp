#include "FileSystem.h"
#include "Application/Utils/wildcard.h"	
#include "System/Console/Trace.h"
#include <algorithm>

T_SimpleList<Path::Alias> Path::aliases_(true) ;

using namespace std ;

Path::Path():gotType_(false),type_(FT_UNKNOWN) {
	path_=(char *)malloc(1) ;
	strcpy(path_,"") ;
} ;

Path::Path(const char *path):gotType_(false),type_(FT_UNKNOWN) {
	path_=(char *)malloc((int)strlen(path)+1) ;
	strcpy(path_,path) ;
} ;

Path::Path(const char *path,FileType type)
	:gotType_(type!=FT_UNKNOWN),type_(type) {
	path_=(char *)malloc((int)strlen(path)+1) ;
	strcpy(path_,path) ;
} ;

Path::Path(const std::string &path):gotType_(false),type_(FT_UNKNOWN) {
	path_=(char *)malloc(path.size()+1) ;
	strcpy(path_,path.c_str()) ;
} ;

Path::Path(const Path &other) {
	path_=(char *)malloc((int)strlen(other.path_)+1) ;
	strcpy(path_,other.path_) ;
	gotType_=other.gotType_;
	type_=other.type_ ;
} ;

Path &Path::operator=(const Path &other) {
	if (this==&other) return *this ;
	char *newPath=(char *)malloc((int)strlen(other.path_)+1) ;
	if (!newPath) return *this ;
	strcpy(newPath,other.path_) ;
	free(path_) ;
	path_=newPath ;
	gotType_=other.gotType_ ;
	type_=other.type_ ;
	return *this ;
} ;

Path::~Path() {
	SYS_FREE (path_) ;
};

std::string Path::GetPath() const
{
	std::string path=path_ ;
	std::string::size_type pos ;

	bool search=true ;
	while ( (search)&&((pos= path.find (":",0))!=string::npos)) {
		string aliasString = path.substr(0,pos);
		const char *aliasPath=resolveAlias(aliasString.c_str()) ;
		string forward ;
		if (aliasPath) {
			forward+=string(aliasPath)+"/" ;
			path=forward+path.substr(pos+1) ;
		} else {
			search=false ;
		} ;
	} ;
	fullPath_=path ;
	return fullPath_ ;
} ;

std::string Path::GetCanonicalPath() {
	std::string copy=GetPath(); 
	std::string::size_type pos ;
	while ((pos=copy.find("\\")) != std::string::npos) {
		std::string rpart=copy.substr(pos+1) ;
		copy=copy.substr(0,pos) ;
		copy+="/" ;
		copy+=rpart ;
	} ;
	return copy ;
} ;

Path Path::Descend(const std::string& leaf)
{
  std::string currentPath = GetPath();
  if (!currentPath.empty() && currentPath[currentPath.size() - 1] != '/') {
      currentPath += "/";
  }
  return Path(currentPath+leaf);
}

void Path::getType() {
	if (!gotType_) {
		gotType_=true ;
		std::string resolved=GetPath() ;
		type_=FileSystem::GetInstance()->GetFileType(resolved.c_str()) ;
	}
} ;

std::string Path::GetName() {

	unsigned int index=0 ;
	for (unsigned int i=0;i<strlen(path_);i++) {
		if (path_[i]=='/') {
			index=i ;
		} ;
	} ;
	if (index!=0) index++ ;
	return std::string(path_+index);
} ;

int Path::Compare(const Path &other) {
	return strcasecmp(path_,other.path_) ;
} ;

bool Path::Exists() {
	getType() ;
	return type_!=FT_UNKNOWN ;
} ;

bool Path::IsFile() {
	getType() ;
	return type_==FT_FILE ;
} ;

bool Path::IsDirectory() {
	getType() ;
	return type_==FT_DIR ;
} ;

bool Path::Matches(const char *pattern) {
	std::string name=GetName() ;
	std::transform(name.begin(), name.end(),name.begin(), ::tolower);
	return wildcardfit(pattern,name.c_str())  == 1 ;
} ;

Path Path::GetParent() {
	std::string current=GetCanonicalPath() ;
	while (current.size()>1 && current[current.size()-1]=='/')
		current.erase(current.size()-1) ;
	std::string::size_type index=current.rfind("/") ;
	if (index==std::string::npos)
		return Path("") ;
	std::string parentPath=(index==0)?"/":current.substr(0,index) ;
	Path parent(parentPath) ;
	return parent ;
}


void Path::SetAlias(const char *alias,const char *path) {
	IteratorPtr<Alias> it(aliases_.GetIterator()) ;
	for (it->Begin();!it->IsDone();it->Next()) {
		Alias &current=it->CurrentItem() ;
		if (!strcmp(current.GetAliasName(),alias)) {
				current.SetPath(path) ;
				return ;	
			} ;
	};
	Alias *a=new Alias(alias,path) ;
	aliases_.Insert(a) ;
} ;

const char *Path::resolveAlias(const char *alias) {
	IteratorPtr<Alias> it(aliases_.GetIterator()) ;
	for (it->Begin();!it->IsDone();it->Next()) {
		Alias &current=it->CurrentItem() ;
		if (!strcmp(current.GetAliasName(),alias)) {
			return current.GetPath() ;	
		} ;
	};
	return 0 ;
} ;
Path::Alias::Alias(const char *alias,const char *path) {
	alias_=alias ;
	path_=path ;
} ;

const char *Path::Alias::GetAliasName() {
	return alias_.c_str() ;
} ;

const char *Path::Alias::GetPath() {
	return path_.c_str() ;
}

void Path::Alias::SetAliasName(const char *alias) {
	alias_=alias ;
} ;

void Path::Alias::SetPath(const char *path) {
	path_=path ;
} ;

int FileSystemService::Copy(const Path &src,const Path &dst)
{
  int count = 0;
  int total=0;

  FileSystem * fs=FileSystem::GetInstance() ;
  I_File *isrc = fs->Open(src.GetPath().c_str(), "r");

  Trace::Log("FS","FileSystemService::Copy %s to %s",
  src.GetPath().c_str(), dst.GetPath().c_str());
  if (!isrc) {
      Trace::Error("Could not open copy source %s", src.GetPath().c_str());
      return -1;
  }

  I_File *idst=fs->Open(dst.GetPath().c_str(),"w");
  if (!idst) {
    Trace::Error("Could not open copy destination %s", dst.GetPath().c_str());
    isrc->Close();
    delete isrc;
    return -1;
  }

  const int fallbackBufferSize=4096;
  const int preferredBufferSize=32*1024;
  char fallbackBuffer[fallbackBufferSize];
  char *allocatedBuffer=(char *)SYS_MALLOC(preferredBufferSize);
  char *buffer=allocatedBuffer?allocatedBuffer:fallbackBuffer;
  int bufferSize=allocatedBuffer?preferredBufferSize:fallbackBufferSize;

  while ((count = isrc->Read(buffer, sizeof(char), bufferSize)) > 0) {
      int written = idst->Write(buffer, sizeof(char), count);
      if (written != count) {
          Trace::Error("Short write copying to %s (%d of %d bytes)",
                       dst.GetPath().c_str(), written, count);
          total = -1;
          break;
      }
      total += written;
  }
  if (isrc->HasError()) {
    Trace::Error("Read failed copying from %s",src.GetPath().c_str());
    total=-1;
  }
  if (!idst->Flush()) {
    Trace::Error("Flush failed copying to %s",dst.GetPath().c_str());
    total=-1;
  }

  isrc->Close();
  idst->Close();
  delete isrc;
  delete idst;
  SAFE_FREE(allocatedBuffer);

  if (total<0) {
    fs->Delete(dst.GetPath().c_str());
  }
  return total;
}

int FileSystemService::Delete(const Path &path) {
    int result = -1;
    std::string pathString = path.GetPath();
    FileSystem * fs = FileSystem::GetInstance();

    if (fs->GetFileType(pathString.c_str()) != FT_UNKNOWN) {
		fs->Delete(pathString.c_str());
        result += 1;
		Trace::Log("FileSystemService"," Delete %s ", pathString.c_str());
    } else {
        Trace::Log("FS Delete","path does not exist: %s", pathString.c_str());
    }

    return result;
}
