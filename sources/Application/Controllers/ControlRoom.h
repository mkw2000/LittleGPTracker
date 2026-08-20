
#ifndef _CONTROLROOM_H_
#define _CONTROLROOM_H_

#include "Foundation/T_Singleton.h"
#include "Services/Controllers/ControlNode.h"
#include <string>
#include <vector>


class ControlRoom:public T_Singleton<ControlRoom>,public ControlNode {
public:
	ControlRoom() ;
	~ControlRoom() ;

	bool Init() ;
	void Close() ;

	bool Attach(const char *nodeUrl,const char *controllerUrl) ;
	AssignableControlNode *GetControlNode(const std::string url) ;

	bool LoadMapping(const char *path) ;
	void RetryMappings() ;

	void Dump() ;

private:
	struct PendingMapping {
		std::string nodeUrl ;
		std::string controllerUrl ;
	} ;

	void QueueMapping(const char *nodeUrl,const char *controllerUrl) ;
	std::vector<PendingMapping> pendingMappings_ ;
} ;
#endif
