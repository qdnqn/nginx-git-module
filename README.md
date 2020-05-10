# nginx-git-module
This module is allowing nginx to serve git repos over http. Module is heavily relying on [libgithttp](https://github.com/qdnqn/libgithttp) and optionally for brokering services on [libgithttp-controller](https://github.com/qdnqn/libgithttp-controller). In the configuration example I have used modified auth_basic module originating from nginx official repo so It can validate users in the same manner that auth_basic does but instead for looking in file, it looks in redis database for username:password (generated with htpasswd).

## So how it works?
Authentication is www-auth basic authentication so no secure way of authentication is provided for now. It works same as auth_basic does but authentication is made against redis database. I have made example available at https://github.com/qdnqn/nginx-basic-auth.



Second part is git handler. So what this module actually does is watching Content-type header and act accordingly. As there are multiple git Content-type header for different actions module distincts request based on them. Based on that proper function is called from [libgithttp](https://github.com/qdnqn/libgithttp) (Check Under the hood section in readme).

Request body is stored in file in /tmp/test.txt. The git client send wants/haves/packfile in body of http request we need that part for processing and sending back proper information so git client can understand our response and do his magic.

## Available commands
Commands you can use from this module are

1. git_redis_host "string" / self-explained 
2. git_redis_port 1(INT) / self-explained
3. git_repo COMPLEX_VALUE / Here is set repository directory name eg. test
4. git_path COMPLEX_VALUE / Here you define path to the directory that holds your repository directories eg /home/user/repos/
5. git_uri COMPLEX_VALUE / You are setting git uri here from request as you need to ommit unnecessary data (Url rewrite?)
6. git_auth / Here you are setting if git authentication is used. 0 if not auth_basic or any other used.
7. git_serve / It calls handler for all the hard work.

## How to build?
Well it's not somewhat easy, for now, to build this module as it's dependand on multiple libraries. First you need to get all dependancies.



You need to compile [libgithttp](https://github.com/qdnqn/libgithttp), [libgithttp-controller](https://github.com/qdnqn/libgithttp-controller) and hiredis (Instructions are found on each repo for compiling and building.).

When you have finished that you can compile your nginx version. I have provided my script for compiling nginx below so you can use it as reference for compiling any nginx module later and also for this one (Placed in root driectory of nginx source).
```
#!/bin/bash

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/wubuntu/ext10/Projects/git-server/libgit2/build
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/wubuntu/ext10/Projects/git-server/git_handler
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/wubuntu/ext10/Projects/git-server/redis

DIR=$(pwd)
BUILDDIR=$DIR/build
NGINX_DIR=nginx
NGINX_VERSION=1.4.7

CFLAGS="-g -O0"    # You can ommit -g and -O0 as it's used in development process

./auto/configure                      \
    --with-debug                      \
    --prefix=$(pwd)/build/nginx       \
    --conf-path=conf/nginx.conf       \
    --error-log-path=logs/error.log   \
    --http-log-path=logs/access.log   \
    --add-dynamic-module=$(pwd)/src/addon/git/ \
    --with-cc-opt="-I$(pwd)/../git_handler -I$(pwd)/../redis" \
    --with-ld-opt="-L$(pwd)/../git_handler -lgithttp -L$(pwd)/../redis -lhiredis"

make install
ln -sf $(pwd)/nginx.conf $(pwd)/build/nginx/conf/nginx.conf
```

## Configuration example
```
http {
  server {
    listen 8888;
    server_name  localhost;
        
    location ~* /git/(?<repo>[a-zA-Z0-9_]+)/(?<git_uri>.*)$ {
        auth_basic_redis_host "127.0.0.1";
        auth_basic_redis_port 6379;
        auth_basic "Realm";
        
        git_redis_host "127.0.0.1";
        git_redis_port 6379;
        git_repo $repo;
        git_path /example/git-server/testing_repos/;
        git_uri $git_uri;
        git_auth 1; # If you use auth_basic you must set 1 to enable user functionality in git_serve (Guest log by default)
        git_serve;
    }
  }
}
```
Now if you try to do this
```
git clone http://localhost:8888/test
```
from git client, you should be able to clone repository from

```
/example/git-server/testing_repos/test.repo
```
Caution: bare repos supported only.