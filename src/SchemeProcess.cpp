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

#include "TaskScheduler.h"
#include "SchemeProcess.h"
#include "SchemeFFI.h"

#include <iosfwd>
#include <iomanip>
#include <stdexcept>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>         /* host to IP resolution       */
#include <errno.h>
#include <stdlib.h>
#include <sys/select.h>
#include "UNIV.h"

// FD_COPY IS BSD ONLY
#ifndef FD_COPY
#define        FD_COPY(f, t)   (void)(*(t) = *(f))
#endif

extern llvm_zone_t* llvm_zone_create(uint64_t);

namespace extemp {
	
	std::vector<SchemeProcess*> SchemeProcess::INSTANCES;
	std::map<scheme*, SchemeProcess*> SchemeProcess::SCHEME_MAP;
	std::map<std::string, SchemeProcess*> SchemeProcess::SCHEME_NAME_MAP;
	bool SchemeProcess::CAPS_THROUGH = 0; //send caps lock through to editor window or block?
	bool SchemeProcess::EXTENDED_ERROR_LOGGING = 0;
	bool SchemeProcess::SCHEME_OPS_LOGGING = 0;		
	bool SchemeProcess::TASK_QUEUE_LOGGING = 0;	
	bool SchemeProcess::SCHEME_EVAL_TIMING = 0;	
	
    SchemeProcess::SchemeProcess(std::string _load_path, std::string _name, int _server_port, bool _banner) :
	    libs_loaded(false),
		load_path(_load_path),
		name(_name),
		threadScheme(), //(&impromptu_task_executer, this), 
		threadServer(), //(&impromptu_server_thread, this),
		guard("scheme_server_guard"), //"Scheme Guard"),
		running(true),
		server_port(_server_port),
		with_banner(_banner)
    {
			if(load_path[load_path.length()-1] != '/') load_path.append("/");
			sc = scheme_init_new();
			default_zone = llvm_zone_create(1024*1024);
			memcpy(sc->name,_name.c_str(),_name.length()+1);
			max_duration = sc->call_default_time;
			//scheme_set_output_port_file(sc, stdout);
			memset(scheme_outport_string,0,256);
			scheme_set_output_port_string(sc,scheme_outport_string,&scheme_outport_string[255]);
			FILE *initscm = fopen(std::string(load_path).append("init.scm").c_str(),"r");
			if(!initscm) {
				std::cout << "ERROR: Could not locate file: init.scm" << std::endl << "Exiting system!!" << std::endl;
				exit(1);
			}else{
				std::cout << "START LOADING INIT.SCM" << std::endl;
				scheme_load_file(sc, initscm);
				std::cout << "FINISHED LOADING INIT.SCM" << std::endl;
			}			
			server_socket = socket(AF_INET, SOCK_STREAM, 0);
			if(server_socket == -1) {
				std::cout << "Error opening extempore socket" << std::endl;
				return;
			}
			int flag = 1;
			int result = setsockopt(server_socket,            /* socket affected */
									IPPROTO_TCP,     /* set option at TCP level */
									TCP_NODELAY,     /* name of option */
									(char *) &flag,  /* the cast is historical cruft */
									sizeof(int));    /* length of option value */
			if (result < 0) {
			}
						
			scheme_define(sc, sc->global_env, mk_symbol(sc, "*imp-envs*"), sc->NIL);
			scheme_define(sc, sc->global_env, mk_symbol(sc, "*callback*"), mk_cptr(sc, mk_cb(this,SchemeProcess,schemeCallback)));
		
			SchemeFFI::I()->initSchemeFFI(sc);
			//threadScheme.SetPriority(CAPThread::kMaxThreadPriority); //kMaxThreadPriority || kDefaultThreadPriority
			//threadServer.SetPriority(CAPThread::kMaxThreadPriority); //kMaxThreadPriority || kDefaultThreadPriority
	}
	
    SchemeProcess::~SchemeProcess()
    {
    }
	
	SchemeProcess* SchemeProcess::I(pthread_t pthread)
	{
		for(int i=0;i<INSTANCES.size();i++) {
			if(INSTANCES[i]->threadScheme.isCurrentThread())
			{
				return INSTANCES[i];
			}
		}
		throw std::runtime_error("Error: SchemeProcess does not exist");		
		return INSTANCES[0];
	}
	
	SchemeProcess* SchemeProcess::I(std::string name)
	{
		if(SCHEME_NAME_MAP.count(name)<1) {
			throw std::runtime_error("Error: SchemeProcess does not exist");
		}		
		return SCHEME_NAME_MAP[name];
	}
			
    SchemeProcess* SchemeProcess::I(int index)
    {
		if(index < INSTANCES.size())
		{
			return INSTANCES[index];
		}else{
			throw std::runtime_error("Error: SchemeProcess does not exist");
		}
    }
	
	SchemeProcess* SchemeProcess::I(scheme* sc)
	{
		if(SCHEME_MAP.count(sc)<1) {
			throw std::runtime_error("Error: SchemeProcess does not exist");
		}				
		return SCHEME_MAP[sc];
	}
	
	bool SchemeProcess::start()
	{
		//set socket options
		int t_reuse = 1;
		setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&t_reuse, sizeof(t_reuse));		
			
		// Bind Server Socket
		struct sockaddr_in address;
		//struct sockaddr_in client_address;
		
		//start socket
		memset((char*) &address, 0, sizeof(address));
		address.sin_family = AF_INET;
		address.sin_port = htons(server_port);
		address.sin_addr.s_addr = htonl(INADDR_ANY); //set server's IP
		//int client_address_size = sizeof(client_address);
		if(bind(server_socket, (struct sockaddr*) &address, sizeof(address)) == -1) {				
			std::cout << "Error binding extempore address to socket" << std::endl;
			//[NativeScheme::LOGVIEW error:[[NSString alloc] initWithString:@"Error binding to socket 7010. Is Impromptu already running? Close any open Impromptu instances and restart"]];
			running = false;			
			return false;
		}
		if(listen(server_socket, 5) == -1) {
			running = false;
			std::cout << "Problem listening to extempore socket" << std::endl;
			return false;
		}
		std::cout << "STARTED EXTEMPORE SERVER" << std::endl;
		threadScheme.create(&impromptu_task_executer, this);
		//threadScheme.Start();
		threadServer.create(&impromptu_server_thread, this);		
		//threadServer.Start();
		guard.init();
		SchemeProcess::SCHEME_NAME_MAP[name] = this;
		SchemeProcess::INSTANCES.push_back(this);
		SchemeProcess::SCHEME_MAP[sc] = this;					
		
		return true;
	}		
	
	void SchemeProcess::stop()
	{
		std::cout << "Stop Scheme Interface" << std::endl;
		running = false;		
		scheme_deinit(sc);
//		if(shutdown(server_socket,SHUT_RDWR)) {
//			std::cerr << "SchemeProcess Error: Error shutting down server socket" << std::endl;
//			perror(NULL);
//		}
//		if(close(server_socket)) {
//			std::cerr << "SchemeProcess Error: Error closing server socket" << std::endl;
//			perror(NULL);
//		}	
//		std::cout << "STOPPED SCHEME SERVER" << std::endl;
	}
	
	long long int SchemeProcess::getMaxDuration()
	{
		return max_duration;
	}	

	void SchemeProcess::setMaxDuration(long long int d)
	{
		max_duration = d;		
		return ;
	}		
	
	// called from scheduling queue thread
    void SchemeProcess::schemeCallback(TaskI* task)
    {
		if(guard.isOwnedByCurrentThread())
		{
			printf("Thread trying to relock during scheme callback. Potential deadlock. Dropping Task!\n");
			return;
		}	

        guard.lock();
		uint64_t current_time = task->getStartTime();
		//printf("ADD NEW CALLBACK: at(%lld) now(%lld)\n",task->getStartTime(),extemp::UNIV::TIME);
		uint64_t duration = task->getDuration();
		Task<SchemeObj*>* t = (Task<SchemeObj*>*) task;
		taskq.push(SchemeTask(current_time, duration, t->getArg(), /*label*/"tmp_label", 1));
		guard.signal(); //Notify();
        guard.unlock();
    }
	
	
	void SchemeProcess::createSchemeTask(void* arg, std::string label, int type)
	{
		if(guard.isOwnedByCurrentThread())
		{
			printf("Thread trying to relock creating scheme task. Potential deadlock. Dropping Task!");
			return;
		}
		guard.lock();
		taskq.push(SchemeTask(extemp::UNIV::TIME, max_duration, arg, label, type));
		guard.signal();
		guard.unlock();
	}
	
	bool SchemeProcess::loadFile(const std::string file, const std::string path)
	{

		FILE* impscm = fopen(std::string(path).append("/").append(file).c_str(),"r");
		if(!impscm) {
			std::cout << "ERROR: Unable to locate file: " << path << "/" << file << std::endl;
			return false;
			//exit(1);
		}else{		
			//std::cout << "LOAD THREAD:" << pthread_self() << std::endl;
			//std::cout << "LOADING FILE: " << path << "/" << file << std::endl << std::flush;		
			scheme_load_file(sc, impscm);
			return true;
		}
	}
	
	void SchemeProcess::addGlobal(char* symbol_name, pointer arg)
	{
		scheme_define(sc, sc->global_env, mk_symbol(sc, symbol_name), arg);
	}
	
	void SchemeProcess::addForeignFunc(char* symbol_name, foreign_func func)
	{
		scheme_define(sc, sc->global_env, mk_symbol(sc, symbol_name), mk_foreign_func(sc, func));
	}
	
	void SchemeProcess::addGlobalCptr(char* symbol_name, void* ptr)
	{
		scheme_define(sc, sc->global_env, mk_symbol(sc, symbol_name), mk_cptr(sc, ptr));		
	}	
		
	/////////////////////////
	//
	// HELPER FUNCTIONS
	//
	//////////////////////////
	
	void SchemeProcess::banner(std::ostream* ss) {
		*ss << std::endl;
		*ss << "##########################################" << std::endl;
		*ss << "##               EXTEMPORE              ##" << std::endl;        
		*ss << "##                                      ##" << std::endl;        		    		
		*ss << "##           andrew@moso.com.au         ##" << std::endl;            
		*ss << "##                                      ##" << std::endl;
		*ss << "##            (c) 2005-2011             ##" << std::endl;    
		*ss << "##                                      ##" << std::endl;        
		*ss << "##########################################" << std::endl;
		*ss << "     ################################" << std::endl;
		*ss << "          ######################" << std::endl;
		*ss << "               ############" << std::endl;
		*ss << "                    ##" << std::endl;
		*ss << std::endl;
		return;
	}
	
	
	// pointer SchemeProcess::deleteMemberFromList(pointer member, pointer list)
	// {
	// 	pointer newlist = sc->NIL;
	// 	for ( ; is_pair(list); list = pair_cdr(list)) {
	// 		if( eqv(pair_car(list), member)) return append(sc,pair_cdr(list),newlist);
	// 		else newlist = cons(sc, pair_car(list), newlist);
	// 	}
	// 	return newlist;
	// }
	// 
	// bool SchemeProcess::findMemberFromList(pointer member, pointer list)
	// {
	// 	for ( ; is_pair(list); list = pair_cdr(list)) {
	// 		if( eqv(pair_car(list), member)) return true;
	// 	}
	// 	return false;
	// }

	
    void SchemeProcess::addSchemeGlobal(char* symbol_name, void* c_ptr)
    {
        scheme_define(sc, sc->global_env, mk_symbol(sc, symbol_name), mk_cptr(sc, c_ptr));            
    }


	void* SchemeProcess::impromptu_task_executer(void* obj_p)
	{
		SchemeProcess* scm = (SchemeProcess*) obj_p;
		//CAGuard& guard = scm->getGuard();
		EXTMonitor& guard = scm->getGuard();
		scheme* sc = scm->getSchemeEnv();
		std::queue<SchemeTask>& q = scm->getQueue();
		bool with_banner = scm->withBanner();
		
		std::stringstream ss;    
		
		std::string load_path = scm->getLoadPath();
		
		scm->loadFile("extempore.scm", load_path.c_str());
		printf("Loaded... extempore.scm\n");
		scm->loadFile("llvmir.scm", load_path.c_str()); 
		printf("Loaded... llvmir.scm\n");
		scm->loadFile("llvmti.scm", load_path.c_str());		
		printf("Loaded... llvmti.scm\n");
		scm->loadFile("comlist.scm", load_path.c_str());
		printf("Loaded... comlist.scm\n");
		scm->loadFile("sort.scm", load_path.c_str());
		printf("Loading... sort.scm\n");
		// scm->loadFile("mbe.scm", [resources UTF8String]);
		// printf("Loading... mbe.scm\n");
		// scm->loadFile("au.scm", [resources UTF8String]);
		// printf("Loading... au.scm\n");
		// scm->loadFile("graphics.scm", [resources UTF8String]);
		// printf("Loading... graphics.scm\n");
		// scm->loadFile("openglconst.scm", [resources UTF8String]);
		// printf("Loading... openglconst.scm\n");
		// scm->loadFile("auvisualui.scm", [resources UTF8String]);
		// printf("Loading... auvisualui.scm\n");
		// scm->loadFile("vdspveclib.scm", [resources UTF8String]);
		// printf("Loading... spaces.scm\n");
		// scm->loadFile("spaces.scm", [resources UTF8String]);
		// printf("Loading... match.scm\n");
		// scm->loadFile("match.scm", [resources UTF8String]);
		// 
				
		scm->setLoadedLibs(true);

		while(scm->getRunning()) {
			while(!q.empty() && scm->getRunning()) {
				//For the moment I've assumed we can get away without locking
				//the thread for q access. We'll see :)  (mutex lock)				
				SchemeTask schemeTask = q.front(); 
				q.pop();
				if(schemeTask.getType() == 2) { //delete old callback env reference
					sc->imp_env->erase((pointer)schemeTask.getPtr());
				}else if(schemeTask.getType() == 5) { //string from local process (MIDI, OSC or similar)
					std::string* evalString = (std::string*) schemeTask.getPtr();
					bool write_reply = false;
					// if this is an ipc call we don't want to write to the message bar
					if(0==strncmp(evalString->c_str(), "(ipc", 4))
					{
						write_reply = false;
					}
					
					// There Shoudn't be a TERMINATION_CHAR for these local messages but we'll check to be safe
					//if(evalString->at(evalString->size()-1) == TERMINATION_CHAR) {
					if(evalString->at(evalString->size()-1) == 10 && evalString->at(evalString->size()-2 == 13)) {
					//	evalString->erase(--evalString->end());
						evalString->erase(--evalString->end());
						evalString->erase(--evalString->end());						
					}else{						
						// this should be the expected result!!  - i.e. do nothing
					}
					uint64_t in_time = UNIV::TIME;
					scheme_load_string(sc, (const char*) evalString->c_str(), in_time, in_time+sc->call_default_time);
					if(SCHEME_EVAL_TIMING) {
					}	
					if(sc->retcode != 0) { //scheme error
						sc->outport->_object._port->rep.string.curr = scm->scheme_outport_string; //this line sets the sc->outport's current index back to the start of scheme_outport_string						
						//write(return_socket, scm->scheme_outport_string, strlen(scm->scheme_outport_string)+1);							
						memset(scm->scheme_outport_string,0,256);
					}																	
					delete evalString;					
				}else if(schemeTask.getType() == 0) { //string from repl loop
					int return_socket = atoi(schemeTask.getLabel().c_str());
					
					std::string* evalString = (std::string*) schemeTask.getPtr();
					bool write_reply = true;
					// if this is an ipc call we don't want to write to the message bar
					if(0==strncmp(evalString->c_str(), "(ipc", 4))
					{
						write_reply = false;
					}
					
					// last char should be ascii TERMINATION_CHAR which we need to delete!
					if(evalString->at(evalString->size()-1) == TERMINATION_CHAR) {
						evalString->erase(--evalString->end());
					}					
					long long in_time = UNIV::TIME;
					scheme_load_string(sc, (const char*) evalString->c_str(), in_time, in_time+sc->call_default_time);
					if(SCHEME_EVAL_TIMING) {
					}												
					if(sc->retcode != 0) { //scheme error
						//result_string[return_index] = scm->scheme_outport_string;
						sc->outport->_object._port->rep.string.curr = scm->scheme_outport_string; //this line sets the sc->outport's current index back to the start of scheme_outport_string						
						write(return_socket, scm->scheme_outport_string, strlen(scm->scheme_outport_string)+1);
						memset(scm->scheme_outport_string,0,256);
					}else{
						ss.str("");		
						UNIV::printSchemeCell(sc, ss, sc->value);
						/// for adding prompt after value
						if(with_banner) {
			                char prompt[28];
							uint64_t time = UNIV::TIME;
							int hours = time / UNIV::HOUR;
							time -= hours * UNIV::HOUR;
							int minutes = time / UNIV::MINUTE;
							time -= minutes * UNIV::MINUTE;
							int seconds = time / UNIV::SECOND;		
			                sprintf(prompt, "\n[extempore %.2d:%.2d:%.2d]: ",hours,minutes,seconds);
			                ss << prompt;
						}
		 				//////////////////////////
						std::string s = ss.str();
						const char* res_str = s.c_str();
						//std::cout << "WRITE: " << s << " '" << res_str << "' length: " << strlen(res_str)+1 << std::endl;
						if(write_reply) write(return_socket, res_str, strlen(res_str)+1);				
					}
					delete evalString;
				}else if(schemeTask.getType() == 1){ //callback
					SchemeObj* obj = (SchemeObj*) schemeTask.getPtr();
					pointer pair = (pointer) obj->getValue();
					pointer func = pair_car(pair);
					pointer args = pair_cdr(pair);
					if(is_closure(func) || is_macro(func) || is_continuation(func) || is_proc(func) || is_foreign(func)) {
						uint64_t in_time = UNIV::TIME;									
						scheme_call(sc, func, args, in_time, in_time+schemeTask.getMaxDuration());
						if(SCHEME_EVAL_TIMING) {
						}																	
						if(sc->retcode != 0) { //scheme error
							sc->outport->_object._port->rep.string.curr = scm->scheme_outport_string; //this line sets the sc->outport's current index back to the start of scheme_outport_string						
							//write(return_socket, scm->scheme_outport_string, strlen(scm->scheme_outport_string)+1);							
							memset(scm->scheme_outport_string,0,256);
						}												
					}else{
						ss.str("");
						UNIV::printSchemeCell(sc, ss, pair);
						std::cerr << "Bad Closure ... " << ss.str() << " Ignoring callback request " << std::endl;
					}
					delete obj;
				}else if(schemeTask.getType() == 3){ //callback with symbol as char*
					SchemeObj* obj = (SchemeObj*) schemeTask.getPtr();
					char* symbolname = (char*) obj->getValue();
					pointer symbolsymbol = mk_symbol(sc,symbolname);
					pointer func = pair_cdr(find_slot_in_env(sc,sc->global_env,symbolsymbol,1));
					pointer args = sc->NIL;
					if(is_closure(func) || is_continuation(func) || is_proc(func) || is_foreign(func)) {
						uint64_t in_time = UNIV::TIME;									
						scheme_call(sc, func, args, in_time, in_time+schemeTask.getMaxDuration());
						if(SCHEME_EVAL_TIMING) {
						}
						if(sc->retcode != 0) { //scheme error
							sc->outport->_object._port->rep.string.curr = scm->scheme_outport_string; //this line sets the sc->outport's current index back to the start of scheme_outport_string						
							//write(return_socket, scm->scheme_outport_string, strlen(scm->scheme_outport_string)+1);							
							memset(scm->scheme_outport_string,0,256);
						}																		
					}else{
						ss.str("");
						extemp::UNIV::printSchemeCell(sc, ss, func);
						std::cerr << "Bad Closure From Symbol ... " << ss.str() << " Ignoring callback request " << std::endl;
					}					
					delete obj;
				}else{
					std::cerr << "ERROR: BAD SchemeTask type!!" << std::endl;					
				}
			}
						
			//rest until we have something to do
			while(q.empty() && scm->getRunning()) {
				
				if(guard.isOwnedByCurrentThread())
				{
					std::cout << "Task Executer thread trying to relock. This shouldn't happen!!!" << std::endl;
				}
								
				// 1000 microseconds = 1 millisecond
 			    usleep(1000);
			}
		}
		std::cout << "Exiting task thread" << std::endl;
		return obj_p;
	}
	
	void* SchemeProcess::impromptu_server_thread(void* obj_p)
    {
        SchemeProcess* scm = (SchemeProcess*) obj_p;
		bool with_banner = scm->withBanner();
		
		// first we need to wait for all libs to load in thread impromptu_task_executer
		while(!scm->loadedLibs())
		{
			usleep(1000);
		}
		
		//CAGuard& guard = scm->getGuard();
		EXTMonitor& guard = scm->getGuard();
        std::queue<SchemeTask>& taskq = scm->getQueue();

		int server_socket = scm->getServerSocket();        
        struct sockaddr_in client_address;
        int client_address_size = sizeof(client_address);
		
        fd_set rfd; //open read sockets (man select for more info)
        std::vector<int> client_sockets;
        std::map<int,std::stringstream*> in_streams;
        FD_ZERO(&rfd); //zero out open sockets
		printf("SERVER SOCKET FD_SET: %d\n",server_socket);
        FD_SET(server_socket, &rfd); //add server socket to open sockets list
		int highest_fd = server_socket+1;		
		printf("FD SIZE=%d  and %d\n",highest_fd,FD_SETSIZE);
        int BUFLEN = 1024;
        char buf[BUFLEN+1];
        while(scm->getRunning()) {
            fd_set c_rfd;
			FD_ZERO(&c_rfd);
			FD_COPY(&rfd,&c_rfd);
			timeval pause;
			pause.tv_sec = 1;
			pause.tv_usec = 0;			
			int res = select(highest_fd, &c_rfd, NULL, NULL, &pause);			
			if(res >= 0) {
			}else{				
				struct stat buf;
				std::vector<int>::iterator pos = client_sockets.begin();
				while(pos != client_sockets.end()) {
					int result = fstat(*pos,&buf); 
					if(result < 0) {
						FD_CLR(*pos,&rfd);
						client_sockets.erase(pos);
						break;
					}
					pos++;
				}
			    printf("%s SERVER ERROR: %s\n",scm->name.c_str(),strerror(errno));				
				continue;
			}
            if(FD_ISSET(server_socket, &c_rfd)) { //check if we have any new accpets on our server socket
                res = accept(server_socket,(struct sockaddr *)&client_address, (socklen_t *) &client_address_size);
                if(res < 0) {
					std::cout << "Bad Accept in Server Socket Handling" << std::endl;
					continue; //continue on error
				}
				if(res >= highest_fd) highest_fd = res+1;
                FD_SET(res, &rfd); //add new socket to the FD_SET
				printf("New Client Connection \n");
                client_sockets.push_back(res);
                in_streams[res] = new std::stringstream;
                std::stringstream* ss = new std::stringstream;
				SchemeProcess::banner(ss);
				uint64_t time = UNIV::TIME;
				int hours = time / UNIV::HOUR;
				time -= hours * UNIV::HOUR;
				int minutes = time / UNIV::MINUTE;
				time -= minutes * UNIV::MINUTE;
				int seconds = time / UNIV::SECOND;
                char prompt[28];
                sprintf(prompt, "[extempore %.2d:%.2d:%.2d]: ",hours,minutes,seconds);
                if(with_banner) *ss << prompt;
                write(res, ss->str().c_str(), ss->str().length()+1);
                continue;
            }
			std::vector<int>::iterator pos = client_sockets.begin();
			
			while(pos != client_sockets.end()) { // check through all fd's for matches against FD_ISSET
                if(FD_ISSET(*pos, &c_rfd)) { //see if any client sockets have data for us
					int sock = *pos;
					std::string evalstr = "\r\n";
					for(int j=0; true; j++) { //read from stream in BUFLEN blocks
						memset(buf, 0, BUFLEN+1);
						res = read(sock, buf, BUFLEN);
						if(res == 0) { //close the socket
							FD_CLR(sock, &rfd);	
							delete(in_streams[sock]);
							in_streams[sock] = 0;
							std::cout << "Close Client Socket" << std::endl;
							pos = client_sockets.erase(pos);							
							close(sock);							
							break;
						}else if(res < 0){
							printf("Error with socket read from extempore process %s",strerror(errno));
							pos++;
							break;
						}
						
						*in_streams[sock] << buf;
						evalstr = in_streams[sock]->str();
												
						//if(evalstr[evalstr.length()-1] == TERMINATION_CHAR) { // 23 here is an end of transmission block (ascii ETB)
						if(evalstr[evalstr.length()-1] == 10 && evalstr[evalstr.length()-2] == 13) {
							in_streams[sock]->str("");							
							pos++;
							break;
						}
						// if we get to 1024 assume we aren't going to get a TERMINATION_CHAR and bomb out
						if(j>(1024*10)) {
							printf("Error reading eval string from server socket. No terminator received before 10M limit.\n");
							in_streams[sock]->str("");														
							evalstr = "";
							break;
						}
					}
					// there can be a number of separate expressions in a single string
					int subexprs = 0;
					for(int k=0;k<evalstr.length()-1;k++) { // remote -1
						// if(evalstr[k] == TERMINATION_CHAR) subexptrs++;
						if(evalstr[k] == 13 && evalstr[k+1] == 10) subexprs++;
					}					
					int subexprspos = 0;
					int subexprsnext = 0;
					for(int y=0;y<subexprs;y++) {
						for( ; subexprsnext<evalstr.length();subexprsnext++) // remove -1
						{
							//if(evalstr[subexprsnext] == TERMINATION_CHAR) break;
							if(evalstr[subexprsnext] == 13 && evalstr[subexprsnext+1] == 10) break;
						}
						if(evalstr != "#break#" && evalstr != "") {
							if(guard.isOwnedByCurrentThread())
							{
								printf("Extempore interpreter server thread trying to relock. Dropping Task!. Let me know andrew@moso.com.au\n");
							}
							else
							{				
								guard.lock();
								char c[8];
								sprintf(c, "%i", sock);
								subexprsnext++;
								std::string* s = new std::string(evalstr.substr(subexprspos,(subexprsnext-subexprspos)));
								//std::cout << extemp::UNIV::TIME << "> SCHEME TASK WITH SUBEXPR:" << y << ">><" << subexprspos << "," << (subexprsnext-subexprspos) << "> " << *s << std::endl;
								subexprspos = subexprsnext;		
								taskq.push(SchemeTask(extemp::UNIV::TIME, scm->getMaxDuration(), s, c, 0));// Task<std::string*>(0l, NULL, new std::string(evalstr), c));
								guard.signal(); //Notify();
								guard.unlock();
								// if this is an ipc call don't wait for a reply
								//if(0==strncmp(s->c_str(), "(ipc", 4)) continue;
							}
						}
					}
                }else{
					pos++;
				}
            }
        }
		//std::cout << "Close any client sockets" << std::endl;
		std::vector<int>::iterator pos = client_sockets.begin();		
		while(pos != client_sockets.end()) { // check through all fd's for matches against FD_ISSET
			int sock = *pos;
			if(sock<0) {
				std::cout << "BAD FILE DESCRIPTOR!" << std::endl;
				pos = client_sockets.erase(pos); // erase returns next pos				
				continue;
			}
			FD_CLR(sock, &rfd);	
			delete(in_streams[sock]);
			in_streams[sock] = 0;
			std::cout << "CLOSE CLIENT-SOCKET" << std::endl;
			close(sock);
			std::cout << "DONE-CLOSING_CLIENT" << std::endl;
			pos = client_sockets.erase(pos); // erase returns next pos			
		}
		if(close(server_socket)) {
			std::cerr << "SchemeProcess Error: Error closing server socket" << std::endl;
			perror(NULL);
		}			
		std::cout << "Exiting server thread" << std::endl;
		return obj_p;
    }
	
	///////////////////////////
	//SCHEME OBJ
	//	
	SchemeObj::SchemeObj(scheme* _sc, pointer _val, pointer _env) : sc(_sc), env(_env)
	{
		if(env != NULL) {
			sc->imp_env->insert(env);// = cons(sc, _env, sc->imp_env);
		}else{
			std::cout << "BANG CRASH SHEBANG" << std::endl;
			exit(0);
		}
		
		values = _val;
	}
	
	SchemeObj::~SchemeObj() 
	{
		if(env != NULL) {
			SchemeProcess::I(sc)->createSchemeTask(env, "destroy SchemeObj", 2);
		}
	}
	
	pointer SchemeObj::getEnvironment() 
	{ 
		return env; 
	}
	
	pointer SchemeObj::getValue() 
	{  //NOTE: REMEMBER THAT getValue() is called from a non-scheme thread: so don't try to change the scheme environment in anyway!!
		return values;
	}
	
	scheme* SchemeObj::getScheme() 
	{ 
		return sc; 
	}
	
} // namespace imp