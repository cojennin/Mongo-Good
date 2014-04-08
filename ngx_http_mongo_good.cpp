#include <cstdlib>
#include <iostream>
#include "/Users/connorjennings/mongo/src/mongo/client/dbclient.h"

extern "C" {
  #include <ngx_config.h>
  #include <ngx_core.h>
  #include <ngx_http.h>

  static ngx_int_t ngx_http_mg_handler(ngx_http_request_t *);
  static char* ngx_http_mg(ngx_conf_t *, ngx_command_t *, void *);
  static void* ngx_http_mg_create_loc_conf(ngx_conf_t *);
  static char* ngx_http_mg_merge_loc_conf(ngx_conf_t *, void *, void *);

  typedef struct {
    ngx_str_t     mongo_server_addr;
    ngx_str_t     mongo_user;
    ngx_str_t     mongo_pass;
  } ngx_http_mg_loc_conf_t;

  static ngx_command_t ngx_http_mg_commands[] = {
    { ngx_string("mongo_server_addr"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_mg_loc_conf_t, mongo_server_addr),
      NULL },

    { ngx_string("mongo_user"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_mg_loc_conf_t, mongo_user),
      NULL },

    { ngx_string("mongo_pass"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_mg_loc_conf_t, mongo_pass),
      NULL },

    ngx_null_command
  };

  static ngx_http_module_t ngx_http_mg_module_ctx = {
    NULL,
    NULL,

    NULL,
    NULL,

    NULL,
    NULL,

    ngx_http_mg_create_loc_conf,
    ngx_http_mg_merge_loc_conf
  };

  static ngx_module_t ngx_http_mg_module = {
    NGX_MODULE_V1,
    &ngx_http_mg_module_ctx,
    ngx_http_mg_commands,
    NGX_HTTP_MODULE,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NGX_MODULE_V1_PADDING
  };

} //End of extern

//Mongo c++ db connection
mongo::DBClientConnection c;

static ngx_int_t
ngx_http_mg_handler(ngx_http_request_t *r)
{
    ngx_int_t    rc;
    ngx_buf_t    *b;
    ngx_chain_t  out;

    ngx_http_mg_loc_conf_t  *mg_cf;

    mgcf = (ngx_http_mg_loc_conf_t *)ngx_http_get_module_loc_conf(r, ngx_http_mg_module);

    if (!(r->method & !(NGX_HTTP_GET|NGX_HTTP_POST))) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    if (r->headers_in.if_modified_since) {
        return NGX_HTTP_NOT_MODIFIED;
    }

    return rc;
}

static char *
ngx_http_mg(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf;
    ngx_http_mg_loc_conf_t *mglcf = (ngx_http_mg_loc_conf_t * )conf;

    clcf = (ngx_http_core_loc_conf_t *) ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_mg_handler;

    return NGX_CONF_OK;
}

static void *
ngx_http_mg_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_mg_loc_conf_t  *conf;

    conf = (ngx_http_mg_loc_conf_t *)ngx_pcalloc(cf->pool, sizeof(ngx_http_mg_loc_conf_t));

    if (conf == NULL) {
        return NGX_CONF_ERROR;
    }

    /*
     * set by ngx_pcalloc():
     * conf->mongo_server_addr = { 0, NULL }
     * conf->mongo_user = { 0, NULL }
     * conf->mongo_pass = { 0, NULL }
     */

    return conf;
}

static char *
ngx_http_mg_merge_loc_conf(ngx_conf_t *cf, void * parent, void * child)
{
    ngx_http_mg_loc_conf_t *prev = (ngx_http_mg_loc_conf_t *) parent;
    ngx_http_mg_loc_conf_t *conf = (ngx_http_mg_loc_conf_t *) child;

    //Merge up all our defaults.
    ngx_conf_merge_str_value(conf->mongo_server_addr, prev->server_addr, "127.0.0.1");
    ngx_conf_merge_str_value(conf->mongo_user, prev->mongo_user, "admin");
    ngx_conf_merge_str_value(conf->mongo_pass, prev->mongo_pass, "");

    //Try connecting to Mongo.
    try {
      c.connect(conf->mongo_server_addr.data);
    } catch( const mongo::DBException &e ) {
      frpintf(stderr, "Problems with connecting to Mongo: %s", e.what() );
      return NGX_CONF_ERROR; //Should you still be able to boot nginx without a connection to mongo?
    }

    return NGX_CONF_OK;
}

