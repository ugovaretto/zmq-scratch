#pragma once
//
// Created by Ugo Varetto on 7/4/16.
//


service
class FileSystem : ServiceObject {
    vector< char > ServiceCall(int ID, const vector< char >& args) {
        ....
    }
};

server
#include "FileSystem.h" //per-object RPCS
RPCServer rs("tcp://*:6667"); //router socket
rs.Add("FileSystem", FileSystem());

client
#include "FileSystemClient.h" //method ids
RPCClient rc("tcp://localhost:6667");
each proxy talks with the remote object directly on a different port
through a REQ socket
RPCProxy p = rc.Create("FileSystem");
vector<string> s = p[LS]("/");