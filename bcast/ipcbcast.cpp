//
// Created by Ugo Varetto on 8/30/16.
//

//IN PROGRESS SINGLE-NODE BROADCAST

#include <cstdlib>
#include <iostream>
#include <thread>
#include <future>
#include <vector>
#include <string>
#include <stdexcept>
#include <set>

#include <unistd.h>

#include <zmq.h>

#include "../utility.h"

using namespace std;

int BindURI(void* ss,
            const string& base,
            int startID,
            int maxProcesses) {
  int s = -1;
  for(s = startID; s != startID + maxProcesses; ++s) {
    if(zmq_bind(ss, (base + to_string(s)).c_str()) == 0) break;
  }
  if(s != startID + maxProcesses) return s;
  return -1;
}

int main(int, char**) {

  const int N = 4;
  const char* PUSH_URI_BASE = "tcp://*:";
  const int ID_BASE = 8888;
  auto worker = [=](int id) {
    void *ctx = ZCheck(zmq_ctx_new());
    void *push = ZCheck(zmq_socket(ctx, ZMQ_PUSH));

    const int bound = BindURI(push, PUSH_URI_BASE, ID_BASE, N);
    if(bound < 0) throw std::runtime_error("Cannot bind");
    set< int > pullids;
    for(int i = ID_BASE; i != ID_BASE + N; ++i) if(i != bound) pullids.insert(i);
    vector< void* > pull;
    for(auto& i: pullids) {
      const string uri = "tcp://localhost:" + to_string(i);
      void* s = ZCheck(zmq_socket(ctx, ZMQ_PULL));
      ZCheck(zmq_connect(s, uri.c_str()));
      pull.push_back(s);
      printf("Connected to %s \n", uri.c_str());
    }
    for (int i = 0; i != N - 1; ++i) {
      ZCheck(zmq_send(push, &id, sizeof(id), 0));
    }
    int r = -1;
    for (int i = 0; i != N - 1; ++i) {
      ZCheck(zmq_recv(pull[i], &r, sizeof(r), 0));
      printf("%d %d\n", r, i);
    }
  };
  vector< future< void > > tasks;
  for(int i = 0; i != N; ++i) {
    tasks.push_back(async(launch::async, worker, i));
  }
  for(auto& t: tasks) t.get();
  return EXIT_SUCCESS;
}