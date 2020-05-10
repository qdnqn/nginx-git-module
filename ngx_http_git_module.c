#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "git2.h"
#include "git_init.h"
#include "g_string.h"
#include "g_http.h"
#include "g_parser.h"
#include "g_objects.h"
#include "g_auth.h"
#include "refs.h"

#include "hiredis.h"

ngx_module_t ngx_http_git_module;

typedef struct {
	ngx_http_complex_value_t   *repo;			// Repo name -> directory where you can find .git	
	ngx_http_complex_value_t   *path; 		// Path to directory holding repo/repos
	ngx_http_complex_value_t   *git_uri;	// Git uri for processing
	ngx_http_complex_value_t   *redis_host;
	ngx_int_t                   redis_port;
	ngx_int_t                   status;
	ngx_int_t										auth;
} ngx_http_git_loc_conf_t;

static ngx_int_t ngx_http_git_handler(ngx_http_request_t *r);
static void *ngx_http_git_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_git_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);
static char *ngx_http_git(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static void ngx_http_sample_put_handler(ngx_http_request_t *r);
void ngxstr_string(ngx_str_t* ngxstr, g_str_t* gstr);
void ngxstr_response(ngx_str_t* ngxstr, g_str_t* gstr);

static ngx_command_t  ngx_http_git_commands[] = {
    { ngx_string("git_repo"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_set_complex_value_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_git_loc_conf_t, repo),
      NULL },
            
    { ngx_string("git_path"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_set_complex_value_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_git_loc_conf_t, path),
      NULL },
      
    { ngx_string("git_uri"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_set_complex_value_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_git_loc_conf_t, git_uri),
      NULL },
     
    { ngx_string("git_auth"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_git_loc_conf_t, auth),
      NULL },
    
    { ngx_string("git_serve"),
      NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS,
      ngx_http_git,
      0,
      0,
      NULL },
      
    { ngx_string("git_redis_host"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_set_complex_value_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_git_loc_conf_t, redis_host),
      NULL },
      
    { ngx_string("git_redis_port"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_git_loc_conf_t, redis_port),
      NULL },
      
      ngx_null_command
};

static ngx_int_t ngx_http_git_handler(ngx_http_request_t *r){
	ngx_buf_t                        *b;
	ngx_int_t                         rc;
	ngx_chain_t                       out;
	ngx_str_t													repo_config, path_config, git_uri_config, git_redis_host;
	ngx_str_t													response, content_type;
	ngx_http_git_loc_conf_t  				 *git_config;
	
	git_config = ngx_http_get_module_loc_conf(r, ngx_http_git_module);
	
  if (r->main->internal) {
    return NGX_DECLINED;
  }
      	
  r->main->internal = 1;
  
	if (git_config->repo) {
		if (ngx_http_complex_value(r, git_config->repo, &repo_config) != NGX_OK) {
				return NGX_HTTP_INTERNAL_SERVER_ERROR;
		}
			
		ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "Repo is set in config (dynamically): %V.", &repo_config);
	}	
	
	if(git_config->path){
		if (ngx_http_complex_value(r, git_config->path, &path_config) != NGX_OK) {
				return NGX_HTTP_INTERNAL_SERVER_ERROR;
		}
			
		ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "Path is set in config: %V.", &path_config);
	}
	
	if (git_config->git_uri) {
		if (ngx_http_complex_value(r, git_config->git_uri, &git_uri_config) != NGX_OK) {
				return NGX_HTTP_INTERNAL_SERVER_ERROR;
		}
			
		ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "Git uri is set in config (dynamically): %V.", &git_uri_config);
	}	
	
	if (git_config->redis_host) {
		if (ngx_http_complex_value(r, git_config->redis_host, &git_redis_host) != NGX_OK) {
				return NGX_HTTP_INTERNAL_SERVER_ERROR;
		}
			
		ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "Git redis host is set in config : %V.", &git_redis_host);
	}	
	  
  /* Generate full path to repo: path+repo (From config variables) */
  
  g_str_t* path = string_init();
  g_str_t* path_c = string_init();
  g_str_t* repo_c = string_init();
  
  string_copy_char_nullterminate(path_c, (char*)path_config.data, path_config.len);
  string_copy_char_nullterminate(repo_c, (char*)repo_config.data, repo_config.len);
    
  string_append(path, "%s%s.repo/", path_c->str, repo_c->str);
   
  ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "Accessing repository: %s.", path->str);
  
  g_http_resp* g_http = NULL;
  git_repository* repo;
  
  if(git_init(&repo, path->str) == GIT_REPO_FAILED)	{
		return NGX_HTTP_BAD_REQUEST;
	}
	
	/* Format and send header */
	
	ngx_table_elt_t *cache_h;
  cache_h = ngx_list_push(&r->headers_out.headers);
  cache_h->hash = 1;
  ngx_str_set(&cache_h->key, "Cache-Control");  
  ngx_str_set(&cache_h->value, "no-cache, max-age=0, must-revalidate");
  
  ngx_table_elt_t *pragma_h;
	pragma_h = ngx_list_push(&r->headers_out.headers);
	pragma_h->hash = 1;
	
	ngx_str_set(&pragma_h->key, "Pragma");  
	ngx_str_set(&pragma_h->value, "no-cache");  

	r->headers_out.status = git_config->status;
	
	/*
	 * Now in work with colleague ngx_http_auth_basic_module(modified) we need to do some administration
	 */
	
	uint8_t allowed = ALLOWED_GUEST;
		
	g_str_t* username = string_init();
	redisContext *c = NULL;
	
	if(git_config->auth == 1){
		struct timeval timeout = { 1, 500000 }; // 1.5 seconds
		c = redisConnectWithTimeout((char*)git_redis_host.data, git_config->redis_port, timeout);
		
		string_copy_char_nullterminate(username, (char*)r->headers_in.user.data, r->headers_in.user.len);
		
		g_http = response_init(username, repo_c, c, 1);	// User logging level
		r->http_r = g_http;
 	} else {
		string_append(username,"guest");
		
		g_http = response_init(username, repo_c, c, 0);  // Guest logging level
		r->http_r = g_http;
	}
	
	allowed = authenticate(g_http);
	
	if(allowed != ALLOWED_USER && allowed != ALLOWED_GUEST){	
		return NGX_HTTP_FORBIDDEN;
	}
	
	/*
	 * Sort out request based on http method used
	 */	
	 
	g_str_t* dflt_response = string_init();
	string_add(dflt_response, "action invalid");
	 
	if (r->method == NGX_HTTP_HEAD){
		ngx_str_set(&content_type, "text/plain");
		ngxstr_response(&response, dflt_response);
		r->headers_out.status = NGX_HTTP_BAD_REQUEST;
	} else if (r->method == NGX_HTTP_GET){
		if (ngx_strncasecmp(r->args.data, (u_char*)"service=git-upload-pack", strlen("service=git-upload-pack")) == 0){
			ngx_str_set(&content_type, "application/x-git-upload-pack-advertisement");
			git_get_refs(g_http, repo, g_http->refs, path);
			ngxstr_response(&response, g_http->refs);
		} else if (ngx_strncasecmp(r->args.data, (u_char*)"service=git-receive-pack", strlen("service=git-upload-pack")) == 0){
			ngx_str_set(&content_type, "application/x-git-receive-pack-advertisement");
			git_set_refs(repo, g_http->refs);
			ngxstr_response(&response, g_http->refs);
		}	else {
			ngxstr_response(&response, dflt_response);
			r->headers_out.status = NGX_HTTP_BAD_REQUEST;
		}
  } else if(r->method == NGX_HTTP_POST) {
		rc = ngx_http_read_client_request_body(r, ngx_http_sample_put_handler);

		if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
			return rc;
		}
		
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Loaded request body.");
		
		if (git_uri_config.len == 15 && ngx_strncasecmp(git_uri_config.data, (u_char*)"git-upload-pack", strlen("git-upload-pack")) == 0) {
			ngx_str_set(&content_type, "application/x-git-upload-pack-result");
			get_packfile(g_http, repo, path, r->http_r->request_file->str);
			
			string_append_hexsign(g_http->output, "NAK\n");
			string_concate(g_http->output, g_http->pack);
			
			ngxstr_response(&response, g_http->output);				
		}	else if(git_uri_config.len == 16 && ngx_strncasecmp(git_uri_config.data, (u_char*)"git-receive-pack", strlen("git-receive-pack")) == 0) {
			ngx_str_set(&content_type, "application/x-git-receive-pack-result");
			save_packfile(g_http, repo, path, r->http_r->request_file->str);
						
			string_copy(g_http->output, g_http->message);			
			
			ngxstr_response(&response, g_http->output);
		}	else {
			ngx_str_set(&content_type, "text/plain");
			ngxstr_response(&response, dflt_response);
			r->headers_out.status = NGX_HTTP_BAD_REQUEST;
		}
  } else {
		ngx_str_set(&content_type, "text/plain");
		ngxstr_response(&response, dflt_response);
		r->headers_out.status = NGX_HTTP_BAD_REQUEST;
  }
    
  /* Hackey hack */
  unsigned char response_local[response.len];
  memcpy(response_local, response.data, response.len);
  response.data = &response_local[0];
  /* /////////////////////////////////////////////////////// */
	
	git_deinit(repo);
	response_clean(g_http);
	
	string_clean(dflt_response);
	string_clean(path);
	string_clean(path_c);
  string_clean(repo_c);
  string_clean(username);
	
	r->http_r = NULL;
		
	r->headers_out.content_type = content_type;
	r->headers_out.content_type_len =content_type.len;	

	rc = ngx_http_send_header(r);

	if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
			return rc;
	}
	
	/* Send body */

	b = ngx_calloc_buf(r->pool);
	if (b == NULL) {
			return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}

	b->pos = response.data;
	b->last = response.data + response.len;
	b->memory = response.len ? 1 : 0;
	b->last_buf = (r == r->main) ? 1 : 0;
	b->last_in_chain = 1;

	out.buf = b;
	out.next = NULL;

	return ngx_http_output_filter(r, &out);
}

static ngx_http_module_t ngx_http_git_module_ctx = {
  NULL,                                 /* preconfiguration */
  NULL,             										/* postconfiguration */
  NULL,                                 /* create main configuration */
  NULL,                                 /* init main configuration */
  NULL,                                 /* create server configuration */
  NULL,                                 /* merge server configuration */
  ngx_http_git_create_loc_conf, 				/* create location configuration */
  ngx_http_git_merge_loc_conf   				/* merge location configuration */
};

ngx_module_t ngx_http_git_module = {
  NGX_MODULE_V1,
  &ngx_http_git_module_ctx, 	   	 /* module context */
  ngx_http_git_commands,           /* module directives */
  NGX_HTTP_MODULE,                 /* module type */
  NULL,                            /* init master */
  NULL,                            /* init module */
  NULL,                            /* init process */
  NULL,                            /* init thread */
  NULL,                            /* exit thread */
  NULL,                            /* exit process */
  NULL,                            /* exit master */
  NGX_MODULE_V1_PADDING
};

static void* ngx_http_git_create_loc_conf(ngx_conf_t *cf){
	ngx_http_git_loc_conf_t  *conf;

	conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_git_loc_conf_t));
	
	if (conf == NULL) {
			return NULL;
	}
	
	conf->status = NGX_CONF_UNSET_UINT;
	conf->auth = NGX_CONF_UNSET_UINT;
	conf->redis_port = NGX_CONF_UNSET_UINT;
	
	return conf;
}

static char* ngx_http_git_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child){
    ngx_http_git_loc_conf_t *prev = parent;
    ngx_http_git_loc_conf_t *conf = child;

    ngx_conf_merge_value(conf->status, prev->status, NGX_HTTP_OK);
    		
    return NGX_CONF_OK;
}

static char* ngx_http_git(ngx_conf_t *cf, ngx_command_t *cmd, void *conf){
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_git_handler;

    return NGX_CONF_OK;
}


void ngxstr_string(ngx_str_t* ngxstr, g_str_t* gstr){
	ngxstr->data = (u_char*)gstr->str;
	ngxstr->len = gstr->size;
}

void ngxstr_response(ngx_str_t* ngxstr, g_str_t* gstr){
	ngxstr->data = (u_char*)gstr->str;
	ngxstr->len = gstr->size-1;	// Remove \0 element from gstr
}

static void ngx_http_sample_put_handler(ngx_http_request_t *r){
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "put handler called");

    int fd = creat("/tmp/test.txt", 0777);

    if(-1 == fd){
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to create mail file.%s", strerror(errno));
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    if(NULL == r->request_body->temp_file){
        /*
         * The entire request body is available in the list of buffers pointed by r->request_body->bufs.
         *
         * The list can have a maixmum of two buffers. One buffer contains the request body that was pre-read along with the request headers.
         * The other buffer contains the rest of the request body. The maximum size of the buffer is controlled by 'client_body_buffer_size' directive.
         * If the request body cannot be contained within these two buffers, the entire body  is writtin to the temp file and the buffers are cleared.
         */
        ngx_buf_t    *buf;

        ngx_chain_t  *cl;


        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "Writing data from buffers.");
        cl = r->request_body->bufs;
        for( ;NULL != cl; cl = cl->next )
        {
            buf = cl->buf;
            if(write(fd, buf->pos, buf->last - buf->pos) < 0)
            {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to allocate response buffer.");
                ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                close(fd);
                return;
            }
        }
    } else {
        /**
         * The entire request body is available in the temporary file.
         *
         */
        size_t ret;
        size_t offset = 0;
        unsigned char data[4096];

        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "Writing data from temp file.");

        while((ret = ngx_read_file(&r->request_body->temp_file->file, data, 4096, offset)) > 0) {
            if(write(fd, data, ret) < 0) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to allocate response buffer.");
                ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                close(fd);
                return;
            }
            
            offset = offset + ret;
        }
    }
    
    ngx_log_error(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "All good.");
		string_append(r->http_r->request_file, "/tmp/test.txt");

    close(fd);
    return;
}
