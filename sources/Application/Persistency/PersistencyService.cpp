#include "PersistencyService.h"
#include "Persistent.h"
#include "Externals/Compression/lz.h"
#include "System/Console/Trace.h"
#include "Foundation/Types/Types.h"

namespace {
const int MAX_PROJECT_DATA_SIZE = 16 * 1024 * 1024;
}

PersistencyService::PersistencyService():Service(MAKE_FOURCC('S','V','P','S')) {
} ;

bool PersistencyService::Save(const char *name) {

    Path filename(name);
	Trace::Log("SAVE", "Beginning save to %s", filename.GetPath().c_str());

    TiXmlDocument doc(filename.GetPath());
    TiXmlElement first("LITTLEGPTRACKER") ;
	TiXmlNode *node=doc.InsertEndChild(first) ;

	// Loop on all registered service
	// accumulating XML flow
	
	IteratorPtr<SubService> it(GetIterator()) ;
	for (it->Begin();!it->IsDone();it->Next()) {
		Persistent *currentItem=(Persistent *)&it->CurrentItem() ;
		Trace::Log("SAVE", "Serializing %s", currentItem->GetNodeName());
		currentItem->Save(node) ;
	} ;

	Trace::Log("SAVE", "Writing project file");
    bool succeeded = doc.SaveFile();
    if (!succeeded) {
		Trace::Error("Could not save project to %s", filename.GetPath().c_str());
	} else {
		Trace::Log("SAVE", "Save completed");
	}
	return succeeded;
};

bool PersistencyService::Load() {

    Path filename("project:lgptsav.dat");
    PersistencyDocument doc(filename.GetPath());

    // Try opening the file

    FileSystem *fs = FileSystem::GetInstance();
    I_File *file = fs->Open(filename.GetPath().c_str(), "r");
    if (!file)
        return false;

    // get file size and read all buffer

    file->Seek(0, SEEK_END);
    int length = file->Tell();
    if ((length <= 0) || (length > MAX_PROJECT_DATA_SIZE)) {
        Trace::Error("Invalid project file size: %d bytes", length);
        file->Close();
        delete file;
        return false;
    }

    unsigned char *compBuffer = (unsigned char *)SYS_MALLOC(length + 1);
    if (!compBuffer) {
        Trace::Error("Could not allocate %d bytes for project data",
                     length + 1);
        file->Close();
        delete file;
        return false;
    }

    file->Seek(0, SEEK_SET);
    int bytesRead = file->Read(compBuffer, 1, length);
    file->Close();
    delete file;
    if (bytesRead != length) {
        Trace::Error("Could not read complete project file (%d of %d bytes)",
                     bytesRead, length);
        SYS_FREE(compBuffer);
        return false;
    }
    compBuffer[length] = 0;

    if (!doc.Parse((char *)compBuffer)) {
        doc.Clear();

        // Get uncompressed buffer size from first byte

        int offset = sizeof(int);
        if (length <= offset) {
            Trace::Error("Project file is neither XML nor a valid legacy save");
            SYS_FREE(compBuffer);
            return false;
        }
        int fullLength;
        memcpy(&fullLength, compBuffer, offset);
        if ((fullLength <= 0) || (fullLength > MAX_PROJECT_DATA_SIZE)) {
            Trace::Error("Invalid uncompressed project size: %d bytes",
                         fullLength);
            SYS_FREE(compBuffer);
            return false;
        }

        // Allocate a buffer to decompress data

        unsigned char *xmlSource = (unsigned char *)SYS_MALLOC(fullLength + 1);
        if (!xmlSource) {
            Trace::Error("Could not allocate space for %d bytes",
                         fullLength + 1);
            SYS_FREE(compBuffer);
            return false;
        }

        int unpacked = LZ_UncompressSafe(compBuffer + offset, length - offset,
                                         xmlSource, fullLength);
        if (unpacked != fullLength) {
            Trace::Error("Invalid compressed project data");
            SYS_FREE(xmlSource);
            SYS_FREE(compBuffer);
            return false;
        }
        xmlSource[fullLength] = 0;

        // Initialize XML document on decompressed buffer
        bool parsed = doc.Parse((char *)xmlSource) != 0;

        SYS_FREE(xmlSource);
        if (!parsed) {
            Trace::Error("Could not parse decompressed project data");
            SYS_FREE(compBuffer);
            return false;
        }
    };
    SYS_FREE(compBuffer);

    TiXmlNode* node = 0;
	node = doc.FirstChild( "LITTLEGPTRACKER" );
	if (!node) {
		Trace::Error("could not find master node") ;
		return false ;
	};

	TiXmlElement* element =node->ToElement();
	node = element->FirstChildElement() ;
	if (node) {
		element = node->ToElement();
		while (element) {
			IteratorPtr<SubService> it(GetIterator()) ;
			for (it->Begin();!it->IsDone();it->Next()) {
				Persistent *currentItem=(Persistent *)&it->CurrentItem() ;
				if (currentItem->Restore(element)) {
					break ;
				} ;
			}
			element = element->NextSiblingElement();
		} ;
	}
	return true ;
};
