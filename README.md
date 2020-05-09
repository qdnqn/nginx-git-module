# nginx-git-module
This is custom nginx-git-module based on http-libgit2 (https://github.com/qdnqn/http-libgit2) library and modified basic_access module (https://github.com/qdnqn/nginx-basic-auth). Module is attempt to give nginx power to serve git repositories over http.

More of git cababilities, this module provides, you can check on https://github.com/qdnqn/http-libgit2

# Configuration example
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