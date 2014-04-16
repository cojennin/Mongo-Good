#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include "mongoc.h"

static ngx_int_t ngx_http_mg_handler(ngx_http_request_t *);
static char* ngx_http_mg(ngx_conf_t *, ngx_command_t *, void *);
static void* ngx_http_mg_create_loc_conf(ngx_conf_t *);
static char* ngx_http_mg_merge_loc_conf(ngx_conf_t *, void *, void *);

typedef struct {
  ngx_str_t     mongo_server_addr;
  ngx_str_t     mongo_database;
  ngx_str_t     mongo_collection;
  ngx_str_t     mongo_user;
  ngx_str_t     mongo_passw;
} ngx_http_mg_conf_t;

static ngx_command_t ngx_http_mg_commands[] = {
  {
    ngx_string("mongo"),
    NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS|NGX_CONF_TAKE1,
    ngx_http_mg,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(ngx_http_mg_conf_t, mongo_server_addr),
    NULL
  },
  {
    ngx_string("mongo_database"),
    NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(ngx_http_mg_conf_t, mongo_database),
    NULL
  },
  {
    ngx_string("mongo_collection"),
    NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(ngx_http_mg_conf_t, mongo_collection),
    NULL
  },
  { ngx_string("mongo_user"),
    NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(ngx_http_mg_conf_t, mongo_user),
    NULL },

  { ngx_string("mongo_passw"),
    NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(ngx_http_mg_conf_t, mongo_passw),
    NULL },

  ngx_null_command
};

static ngx_http_module_t ngx_http_mg_module_ctx = {
  NULL,
  NULL, //postconfiguration

  NULL,
  NULL,

  NULL,
  NULL,

  ngx_http_mg_create_loc_conf,
  ngx_http_mg_merge_loc_conf
};

ngx_module_t ngx_http_mg_module = {
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

static ngx_str_t ngx_http_json_type = ngx_string("application/json");
static ngx_str_t ngx_default_server_addr = ngx_string("127.0.0.1");

static ngx_int_t
ngx_http_mg_handler(ngx_http_request_t *r)
{
    ngx_int_t    rc;
    ngx_http_mg_conf_t  *mgcf;
    ngx_http_complex_value_t cv;
    //Mongoc related vars
    mongoc_client_t *client;
    mongoc_collection_t *collection;
    mongoc_cursor_t *cursor;

    //Bson related vars
    bson_error_t error;
    const bson_t *doc;
    bson_t doc_response = BSON_INITIALIZER;
    bson_t query;
    char* response;

    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_POST))) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    mgcf = ngx_http_get_module_loc_conf(r, ngx_http_mg_module);

    mongoc_init(); //Initialize mongoc
    client = mongoc_client_new(mgcf->mongo_server_addr.data); //Connect to db

    //Guess a 500 should suffice?
    if(!client) {
      return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    bson_init( &query ); //init bson

    //https://github.com/mongodb/mongo-c-driver/blob/master/doc/mongoc_collection_find.txt
    collection = mongoc_client_get_collection(client, mgcf->mongo_database.data, mgcf->mongo_collection.data); //Get our requested collection

    //Also a 500
    if(!collection) {
      return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    cursor = mongoc_collection_find(collection, MONGOC_QUERY_NONE, 0, 0, 0, &query, NULL, NULL);

    while(!mongoc_cursor_error(cursor, &error) && mongoc_cursor_more(cursor)) {
      if(mongoc_cursor_next(cursor, &doc)) {
        bson_concat(&doc_response, doc); //Concatenate the bson documents into one large document.
      }
    }

    char* str = bson_as_json(&doc_response, NULL);

    //Ok, need a large enough string to contain our docs + extra info.
    response = (char*)ngx_palloc(r->pool, strlen(str) + 20 );

    strncpy(response, "{\"q_results\" : [", 16);
    strncpy(response + 16, str, strlen(str));
    strncpy(response + 16 + strlen(str), "] }\0", 4); //Wrap up and terminate.

    bson_free(str); //Free up str.

    if(mongoc_cursor_error(cursor, &error)) {
      return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    bson_destroy(&query);
    mongoc_cursor_destroy(cursor);
    mongoc_collection_destroy(collection);
    mongoc_client_destroy(client);

    ngx_memzero(&cv, sizeof(ngx_http_complex_value_t));

    cv.value.len = strlen(response);
    cv.value.data = response;

    if (r->headers_in.if_modified_since) {
        return NGX_HTTP_NOT_MODIFIED;
    }

    return ngx_http_send_response(r, NGX_HTTP_OK, &ngx_http_json_type, &cv);
}

static char *
ngx_http_mg(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
  ngx_http_core_loc_conf_t *clcf;

  clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
  clcf->handler = ngx_http_mg_handler;

  return NGX_CONF_OK;
}


static void *
ngx_http_mg_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_mg_conf_t  *conf;

    conf = (ngx_http_mg_conf_t *)ngx_pcalloc(cf->pool, sizeof(ngx_http_mg_conf_t));

    if (conf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     * conf->mongo_server_addr = { 0, NULL }
     * conf->mongo_database = { 0, NULL }
     * conf->mongo_collection = { 0, NULL }
     * conf->mongo_user = { 0, NULL }
     * conf->mongo_passw = { 0, NULL }
     */

    return conf;
}

static char *
ngx_http_mg_merge_loc_conf(ngx_conf_t *cf, void * parent, void * child)
{
    ngx_http_mg_conf_t *prev = (ngx_http_mg_conf_t *) parent;
    ngx_http_mg_conf_t *conf = (ngx_http_mg_conf_t *) child;

    //Merge up all our defaults.
    ngx_conf_merge_str_value(conf->mongo_server_addr, prev->mongo_server_addr, "mongodb://127.0.0.1");
    ngx_conf_merge_str_value(conf->mongo_database, prev->mongo_database, "test");
    ngx_conf_merge_str_value(conf->mongo_collection, prev->mongo_collection, "test");
    ngx_conf_merge_str_value(conf->mongo_user, prev->mongo_user, "");
    ngx_conf_merge_str_value(conf->mongo_passw, prev->mongo_passw, "");

    return NGX_CONF_OK;
}

