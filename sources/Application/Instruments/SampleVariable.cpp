#include "SampleVariable.h"
#include "SamplePool.h"

SampleVariable::SampleVariable(const char *name,FourCC id):WatchedVariable(name,id,0,0,-1) {
	SamplePool *pool=SamplePool::GetInstance() ;
	list_.char_=pool->GetNameList() ;
	listSize_=pool->GetNameListSize() ;
	pool->AddObserver(*this) ;
} ;

SampleVariable::~SampleVariable() {
	SamplePool *pool=SamplePool::GetInstance() ;
	pool->RemoveObserver(*this) ;
} ;

void SampleVariable::Update(Observable &o,I_ObservableData *d) {
	SamplePoolEvent *e=(SamplePoolEvent *)d ;
	// if we recieved notification that an element has been removed
	// we shift down all the index above the removed element
	if (e->type_==SPET_DELETE) {
		if (value_.index_==e->index_) {
			value_.index_=-1 ;
			onChange() ;
		} else if (value_.index_>e->index_) {
			value_.index_-- ;
		} ;
	} else if (e->type_==SPET_REPLACE && value_.index_==e->index_) {
		onChange() ;
	} ;
	SamplePool *pool=(SamplePool*)&o ;
	list_.char_=pool->GetNameList() ;
	listSize_=pool->GetNameListSize() ;
} ;
