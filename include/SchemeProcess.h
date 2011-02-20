/*
 * Copyright (c) 2011, Andrew Sorensen
 *
 * All rights reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, 
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation 
 *    and/or other materials provided with the distribution.
 *
 * Neither the name of the authors nor other contributors may be used to endorse
 * or promote products derived from this software without specific prior written 
 * permission.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLEXTD. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef SCHEME_PROCESS_H
#define SCHEME_PROCESS_H

#include "Scheme.h"
#include "SchemePrivate.h"	
#include <string>
#include "Task.h"
#include <queue>
#include <map>
#include <sstream>
#include "EXTLLVM.h"

#define pair_caar(p) pair_car(pair_car(p))
#define pair_cadr(p) pair_car(pair_cdr(p))
#define pair_cdar(p) pair_cdr(pair_car(p))
#define pair_cddr(p) pair_cdr(pair_cdr(p))
#define pair_cadar(p) pair_car(pair_cdr(pair_car(p)))
#define pair_caadr(p) pair_car(pair_car(pair_cdr(p)))
#define pair_cdaar(p) pair_cdr(pair_car(pair_car(p)))
#define pair_caddr(p) pair_car(pair_cdr(pair_cdr(p)))
#define pair_cddar(p) pair_cdr(pair_cdr(pair_car(p)))
#define pair_cdddr(p) pair_cdr(pair_cdr(pair_cdr(p)))
#define pair_cadddr(p) pair_car(pair_cdr(pair_cdr(pair_cdr(p))))
#define pair_cddddr(p) pair_cdr(pair_cdr(pair_cdr(pair_cdr(p))))
#define pair_caddddr(p) pair_car(pair_cdr(pair_cdr(pair_cdr(pair_cdr(p)))))
#define pair_cdddddr(p) pair_cdr(pair_cdr(pair_cdr(pair_cdr(pair_cdr(p)))))
#define pair_cadddddr(p) pair_car(pair_cdr(pair_cdr(pair_cdr(pair_cdr(pair_cdr(p))))))
#define pair_cddddddr(p) pair_cdr(pair_cdr(pair_cdr(pair_cdr(pair_cdr(pair_cdr(p))))))
#define pair_caddddddr(p) pair_car(pair_cdr(pair_cdr(pair_cdr(pair_cdr(pair_cdr(pair_cdr(p)))))))

#define TERMINATION_CHAR 23

namespace extemp {
	
    class SchemeTask {	
    public:
	SchemeTask(uint64_t _time, uint64_t _max_duration, void* _ptr, std::string _label, int _type) : time(_time), max_duration(_max_duration), ptr(_ptr), label(_label), type(_type) {}
	uint64_t getTime() { return time; }
	uint64_t getMaxDuration() { return max_duration; }
	void* getPtr() { return ptr; }
	std::string getLabel() { return label; }
	int getType() { return type; }
				
    private:
	uint64_t time;
	uint64_t max_duration;
	void* ptr;
	std::string label;
	int type; // 0 = repl task,  1 = callback task,  2 = destroy env task
    };

    class SchemeProcess {
    public:
	SchemeProcess(std::string _load_path, std::string _name, int server_port=7010, bool banner=false);
	~SchemeProcess();
	static SchemeProcess* I(int index=0);
	static SchemeProcess* I(std::string name);
	static SchemeProcess* I(scheme* sc);
	static SchemeProcess* I(pthread_t);

	//Thread functions
	static void* impromptu_server_thread(void* obj_p);
	static void* impromptu_task_executer(void* obj_p);		
		
	long long int getMaxDuration();
	void setMaxDuration(long long int);		
	bool loadFile(const std::string file, const std::string path);
	void addGlobal(char* symbol_name, pointer arg);		
	void addForeignFunc(char* symbol_name, foreign_func func);
	void addGlobalCptr(char* symbol_name, void* ptr);
	void schemeCallback(TaskI* task);
	void createSchemeTask(void* arg, std::string label, int taskType);
	bool isServerThreadRunning() { return threadServer.isRunning(); }
	bool isSchemeThreadRunning() { return threadScheme.isRunning(); }
		
	void stop();
	bool start();
	bool withBanner() { return with_banner; }
		
	std::string getLoadPath() { return load_path; };	
	//pointer schemeApply(pointer func, pointer args);
	pointer deleteMemberFromList(pointer member, pointer list);	
	bool findMemberFromList(pointer member, pointer list);
	void addSchemeGlobal(char* symbol_name, void* c_ptr);			
	std::string eval(char* evalString);
	scheme* getSchemeEnv() { return sc; }
	void testCall(TaskI* task);
	void repl();
	std::string getEvalString();
	//CAGuard& getGuard() { return guard; }
	EXTMonitor& getGuard() { return guard; }
	//std::map<int, std::string>& getResultStrings() { return result_string; }
	bool getRunning() { return running; }
	//static void printSchemeCons(scheme* sc, std::stringstream& ss, pointer cons, bool full = false, bool stringquotes = true);
	static void banner(std::ostream* ss);
	int getServerSocket() { return server_socket; }		
	int getServerPort() { return server_port; }
	std::queue<SchemeTask>& getQueue() { return taskq; }
	llvm_zone_t* getDefaultZone() { return default_zone; }
		
	std::string getName() { return name; }
	void setLoadedLibs(bool v) { libs_loaded = v; }
	bool loadedLibs() {return libs_loaded; }
	//std::vector<int>* getClientSockets() { return &client_sockets; }
		
	char scheme_outport_string[256];		
	static bool CAPS_THROUGH; //send caps lock through to editor window or block?
		
	static std::vector<SchemeProcess*> INSTANCES;		
	static bool EXTENDED_ERROR_LOGGING;
	static bool SCHEME_OPS_LOGGING;				
	static bool TASK_QUEUE_LOGGING;		
	static bool SCHEME_EVAL_TIMING;				
	static std::map<scheme*, SchemeProcess*> SCHEME_MAP;
	static std::map<std::string, SchemeProcess*> SCHEME_NAME_MAP;

//		llvm::Module* M;
//		llvm::ExistingModuleProvider* MP;
//		llvm::ExecutionEngine* EE;				
		
    private:
	bool libs_loaded;
	std::string load_path;			
	std::string name;
	scheme* sc;		
	EXTThread threadScheme;
	EXTThread threadServer;
	EXTMonitor guard;
	//CAPThread threadScheme;				
	//CAPThread threadServer;
	//CAGuard guard;				
	bool running;
	int server_port;
	bool with_banner;
	uint64_t max_duration;				
	int server_socket;				
	std::queue<SchemeTask> taskq;
	llvm_zone_t* default_zone;
		
	//std::map<int, std::string> result_string;
	//std::vector<int> client_sockets;
	//static vars
    };
	
    class SchemeObj{
    public:
	SchemeObj(scheme* _sc, pointer _val, pointer _env = NULL);
	~SchemeObj();
	pointer getEnvironment();
	pointer getValue();
	scheme* getScheme();
	//pointer copyList(scheme* _sc, pointer newlist, pointer val, int* i);
	//int totalItems(pointer list, int res =0);		
		
    private:
	scheme* sc;
	pointer env;
	pointer values;
    };
	

} //End Namespace

#endif
