#pragma once

#include <App/App.h>

#if defined(DEBUG) || defined(_DEBUG)                                                                                                                                                            
#define _CRTDBG_MAP_ALLOC          
#include <cstdlib>             
#include <crtdbg.h>               
#endif 

class ShapesApp : public TasksInitializer {
public:
	ShapesApp() = default;
	ShapesApp(const ShapesApp& rhs) = delete;
	ShapesApp& operator=(const ShapesApp& rhs) = delete;
	void InitTasks(App& app) noexcept override;
};

