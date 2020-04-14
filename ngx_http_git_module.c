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
#include "refs.h"


ngx_module_t ngx_http_git_module;

typedef struct {
    ngx_int_t                  status;
    ngx_http_complex_value_t  *text;
} ngx_http_git_loc_conf_t;

static ngx_int_t ngx_http_git_handler(ngx_http_request_t *r);
static void *ngx_http_git_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_git_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);
static char *ngx_http_git(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static void ngx_http_sample_put_handler(ngx_http_request_t *r);

static ngx_command_t  ngx_http_git_commands[] = {

    { ngx_string("custom_body"),
      NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS,
      ngx_http_git,
      0,
      0,
      NULL },

    { ngx_string("custom_body_status"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_git_loc_conf_t, status),
      NULL },

    { ngx_string("custom_body_text"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_set_complex_value_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_git_loc_conf_t, text),
      NULL },

      ngx_null_command
};

static ngx_int_t ngx_http_git_handler(ngx_http_request_t *r){
	ngx_buf_t                        *b;
	ngx_int_t                         rc;
	ngx_str_t                         text;
	ngx_str_t													content_type;
	ngx_chain_t                       out;
	ngx_http_git_loc_conf_t  				 *hlcf;
	
  if (r->main->internal) {
    return NGX_DECLINED;
  }
  	
  r->main->internal = 1;
   
  /*
   * Variable of interest: r
   * It-s interesting properties:
   * -> r->method: HTTP method used in request headers
   * -> r->uri: property containing uri data, r->uri.data contains URL
   * -> r->headers_in.host: property r->headers_in.host->value holds the data about host in request header
   * -> Way to copy u_str to ngx_str_t: (u_char)h->value.data = (size_t)r->uri.data; && h->value.len = r->uri.len;
   * -> Handling arugments: h->value.data = r->args.data; && h->value.len = r->args.len;
   */
   
   /* Rijesiti problem kopiranja ngx_str_set() -> ne radi sa dinamicki alociranim pointerima !!!!!!!!! KOPIRA SAMO 6 */
  
  g_http_resp* g_http = response_init();
  r->http_r = g_http;
      
  git_repository* repo;	
  
	g_str_t* path = string_init();
	string_add(path, "/home/wubuntu/ext10/Projects/git-server/test.repo/");
	
	if(git_init(&repo, path->str) == GIT_REPO_FAILED)	{
		return NGX_HTTP_BAD_REQUEST;
	}
	
	text.data = NULL;
	text.len = 3;
    
  if (r->method == NGX_HTTP_HEAD){
		
	} else if (r->method == NGX_HTTP_GET){
		if (r->uri.len == 10 && ngx_strncasecmp(r->uri.data, (u_char*)"/info/refs", strlen("/info/refs")) == 0){
			if (ngx_strncasecmp(r->args.data, (u_char*)"service=git-upload-pack", strlen("service=git-upload-pack")) == 0){
				// Git advertise server capability for FETCH
				git_request = GIT_REQ_GET_REFS;
								
				git_get_refs(g_http, repo, g_http->refs, path);
							
				text.data = (u_char*) g_http->refs->str;
				text.len = g_http->refs->size-1;
			} else if (ngx_strncasecmp(r->args.data, (u_char*)"service=git-receive-pack", strlen("service=git-upload-pack")) == 0){
				// Git advertise server capability for PUSH
				git_request = GIT_REQ_SET_REFS;
								
				git_set_refs(repo, g_http->refs);
				
				text.data = (u_char*)g_http->refs->str;
				text.len = g_http->refs->size-1;
			}	else {
				
			}
		}	else if (r->uri.len == 5 && ngx_strncasecmp(r->uri.data, (u_char*)"/HEAD", strlen("/HEAD")) == 0){
			
		} else {
			
		}
  } else if(r->method == NGX_HTTP_POST) {
		rc = ngx_http_read_client_request_body(r, ngx_http_sample_put_handler);

		if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
			return rc;
		}
		
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Now rest of code");
		
		if (r->uri.len == 16 && ngx_strncasecmp(r->uri.data, (u_char*)"/git-upload-pack", strlen("/git-upload-pack")) == 0) {
			git_request = GIT_REQ_UPLOAD_PACK;
			  			
			ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "PACKING DATA AND TRYING TO SEND!!!");
			get_packfile(g_http, repo, path, r->http_r->request_file->str);
			
			string_append_hexsign(g_http->output, "NAK\n");
			string_concate(g_http->output, g_http->pack);
									
			text.data = (u_char*)g_http->output->str;
			text.len = g_http->output->size-1;
		}	else if(r->uri.len == 17 && ngx_strncasecmp(r->uri.data, (u_char*)"/git-receive-pack", strlen("/git-receive-pack")) == 0) {
			// Process received PACK file
			git_request = GIT_REQ_RECEIVE_PACK;
			  			
			ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "UNPACKING DATA AND TRYING TO INSERT INTO SERVER REPO!!!");
			save_packfile(g_http, repo, path, r->http_r->request_file->str);
						
			string_copy(g_http->output, g_http->message);			
			
			text.data = (u_char*)g_http->output->str;
			text.len = g_http->output->size-1;
		}	else {
		}
  } else {
		//ngx_str_set(&h->value, "OTHER");
  }
  
  if (ngx_http_discard_request_body(r) != NGX_OK) {
			return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}

	hlcf = ngx_http_get_module_loc_conf(r, ngx_http_git_module);

	/* make up output text */

	/*if (hlcf->text) {
			if (ngx_http_complex_value(r, hlcf->text, &text) != NGX_OK) {
					return NGX_HTTP_INTERNAL_SERVER_ERROR;
			}

			ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
										 "http hello_world text: \"%V\"", &text);

	} else {
			//ngx_str_set(&text, "Hello, world!\n");
			text.data = (u_char*)g_http.refs.str;
			text.len = g_http.refs.size;
	}*/
	
	/* Format and send header */
	
	ngx_table_elt_t *cache_h;
  cache_h = ngx_list_push(&r->headers_out.headers);
  cache_h->hash = 1;
  ngx_str_set(&cache_h->key, "Cache-Control");  
  ngx_str_set(&cache_h->value, "no-cache, max-age=0, must-revalidate");  

	r->headers_out.status = hlcf->status;

	if (git_request == GIT_REQ_GET_REFS) {
		ngx_str_set(&content_type, "application/x-git-upload-pack-advertisement");
		
		//r->headers_out.content_length_n = text.len;
		r->headers_out.content_type = content_type;
		r->headers_out.content_type_len =content_type.len;	
	}	else if (git_request == GIT_REQ_SET_REFS) {
		ngx_str_set(&content_type, "application/x-git-receive-pack-advertisement");
		
		r->headers_out.content_type = content_type;
		r->headers_out.content_type_len =content_type.len;	
	}	else if (git_request == GIT_REQ_UPLOAD_PACK) {		
		ngx_table_elt_t *transf_enc_h;
		transf_enc_h = ngx_list_push(&r->headers_out.headers);
		transf_enc_h->hash = 1;
		
		ngx_str_set(&transf_enc_h->key, "Pragma");  
		ngx_str_set(&transf_enc_h->value, "no-cache");
		
		ngx_str_set(&content_type, "application/x-git-upload-pack-result");
		
		//r->headers_out.content_length_n = text.len;												// Chunked response so no length!
		r->headers_out.content_type = content_type;
		r->headers_out.content_type_len =content_type.len;	
	} else if (git_request == GIT_REQ_RECEIVE_PACK) {		
		ngx_table_elt_t *transf_enc_h;
		transf_enc_h = ngx_list_push(&r->headers_out.headers);
		transf_enc_h->hash = 1;
		
		ngx_str_set(&transf_enc_h->key, "Pragma");  
		ngx_str_set(&transf_enc_h->value, "no-cache");
				
		ngx_str_set(&content_type, "application/x-git-receive-pack-result");
		
		//r->headers_out.content_length_n = text.len;												// Chunked response so no length!
		r->headers_out.content_type = content_type;
		r->headers_out.content_type_len =content_type.len;	
	} else {
		
	}

	rc = ngx_http_send_header(r);

	if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
			return rc;
	}
	
	/* send body */

	b = ngx_calloc_buf(r->pool);
	if (b == NULL) {
			return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}

	b->pos = text.data;
	b->last = text.data + text.len;
	b->memory = text.len ? 1 : 0;
	b->last_buf = (r == r->main) ? 1 : 0;
	b->last_in_chain = 1;

	out.buf = b;
	out.next = NULL;
	
	//string_clean(path);
	//response_clean(g_http);
	//git_deinit(repo);

	return ngx_http_output_filter(r, &out);
}

static void ngx_http_sample_put_handler(ngx_http_request_t *r)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "put handler called");

    int fd = creat("/home/wubuntu/test.txt", 0644);

    if(-1 == fd)
    {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to create mail file.%s", strerror(errno));
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    if(NULL == r->request_body->temp_file)
    {
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
    }
    else
    {
        /**
         * The entire request body is available in the temporary file.
         *
         */
        size_t ret;
        size_t offset = 0;
        unsigned char data[4096];

        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "Writing data from temp file.");

        while(  (ret = ngx_read_file(&r->request_body->temp_file->file, data, 4096, offset)) > 0)
        {
            if(write(fd, data, ret) < 0)
            {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to allocate response buffer.");
                ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                close(fd);
                return;
            }
            offset = offset + ret;
        }
    }
    
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "All good.");
		string_append(r->http_r->request_file, "/home/wubuntu/test.txt");

    close(fd);
    return;
}

/* Filter */
static ngx_int_t ngx_http_git_init(ngx_conf_t *cf)
{
  ngx_http_handler_pt *h;
  ngx_http_core_main_conf_t *cmcf;

  cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

  h = ngx_array_push(&cmcf->phases[NGX_HTTP_ACCESS_PHASE].handlers);
  if (h == NULL) {
    return NGX_ERROR;
  }

  *h = ngx_http_git_handler;

  return NGX_OK;
}

static ngx_http_module_t ngx_http_git_module_ctx = {
  NULL,                                 /* preconfiguration */
  ngx_http_git_init,             				/* postconfiguration */
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

static void *
ngx_http_git_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_git_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_git_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     conf->text = NULL;
     */

    conf->status = NGX_CONF_UNSET;

    return conf;
}


static char *
ngx_http_git_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_git_loc_conf_t *prev = parent;
    ngx_http_git_loc_conf_t *conf = child;

    ngx_conf_merge_ptr_value(conf->text, prev->text, NULL);
    ngx_conf_merge_value(conf->status, prev->status, NGX_HTTP_OK);

    return NGX_CONF_OK;
}


static char *
ngx_http_git(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    clcf->handler = ngx_http_git_handler;

    return NGX_CONF_OK;
}

