#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include "mongoc.h"

static ngx_int_t ngx_http_mg_handler(ngx_http_request_t *);
static char* ngx_http_mg(ngx_conf_t *, ngx_command_t *, void *);
static void* ngx_http_mg_create_loc_conf(ngx_conf_t *);
static char* ngx_http_mg_merge_loc_conf(ngx_conf_t *, void *, void *);
static ngx_str_t application_type = ngx_string("application/json");

typedef struct {
  ngx_str_t     mongo_server_addr;
  ngx_str_t     mongo_database;
  ngx_str_t     mongo_collection;
  ngx_str_t     mongo_user;
  ngx_str_t     mongo_passw;
} ngx_http_mg_conf_t;

typedef struct {
  ngx_str_t     error_msg;
  ngx_int_t     error_code;
} error_s

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

static ngx_int_t
ngx_http_mg_handler(ngx_http_request_t *r)
{
    ngx_int_t    rc;
    ngx_http_mg_conf_t  *mgcf;
    ngx_http_complex_value_t cv;
    char* response;
    error_s* error;

    //Let's assume we can handle the following.
    //GET will get
    //POST will create/update
    //Todo: What about PUT?
    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_POST))) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    if (r->headers_in.if_modified_since) {
        return NGX_HTTP_NOT_MODIFIED;
    }

    mgcf = ngx_http_get_module_loc_conf(r, ngx_http_mg_module);

    //If we've got a GET request, process it
    if(r->method == NGX_HTTP_GET) {
      if(ngx_http_mg_handle_get_request(r, mgcf, &cv, error) == NULL) {
        ngx_memzero(&cv, sizeof(ngx_http_complex_value_t));
        cv.value.len = error->error_msg.len;
        cv.value.data = error->error_msg;

        return ngx_http_send_response(r, error->error_code, &application_type, &cv);
      }
    }

    //Duplicative, can refactor.
    if(r->method == NGX_HTTP_POST) {
      if(ngx_http_mg_handle_post_request(r, mgcf, &cv, error) == NULL) {
        ngx_memzero(&cv, sizeof(ngx_http_complex_value_t));
        cv.value.len = error->error_msg.len;
        cv.value.data = error->error_msg;

        return ngx_http_send_response(r, error->error_code, &application_type, &cv);
      }
    }

    return ngx_http_send_response(r, NGX_HTTP_OK, &application_type, &cv);
}

//Build out the complex value from our query and modify our
//request appropriately (handle the application type, etc).
static char*
ngx_http_mg_handle_get_request(ngx_http_request_t *r, ngx_http_mg_conf_t mgcf*, ngx_http_complex_value_t* cv, error_s* ngx_mg_req_error) {
    //Mongoc related vars
    mongo_client_t *client;
    mongoc_collection_t *collection;
    mongoc_cursor_t *cursor;

    //Dealing with bson here.
    bson_error_t error;
    const bson_t *doc;
    bson_t doc_response = BSON_INITIALIZER;
    bson_t query;

    //Init our client and connect
    mongoc_init();
    client = mongoc_client_new(mgcf->mongo_server_addr.data); //Connect to db. Let's assume for the moment all actions require connection.

    bson_init( &query ); //init bson

    //https://github.com/mongodb/mongo-c-driver/blob/master/doc/mongoc_collection_find.txt
    collection = mongoc_client_get_collection(client, mgcf->mongo_database.data, mgcf->mongo_collection.data); //Get our requested collection

    //Also a 500
    if(!collection) {
      ngx_mg_req_error = malloc(sizeof(error_s));
      ngx_mg_req_error->error_msg = (ngx_str_t)ngx_string("{ \"ok\" : false, \"reason\" : \"Unable to get collection.\" }");
      ngx_mg_req_error->error_code = NGX_HTTP_INTERNAL_SERVER_ERROR;
      return NULL;
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

    //If we couldn't allocate, fail.
    if( !response ) {
      ngx_mg_req_error = malloc(sizeof(error_s));
      ngx_mg_req_error->error_msg = (ngx_str_t)ngx_string("{ \"ok\" : false, \"reason\" : \"Unable to allocate enough memory for a response.\" }");
      ngx_mg_req_error->error_code = NGX_HTTP_INTERNAL_SERVER_ERROR;
      return NULL;
    }

    //Get rid of strncpy, we're well aware of the size being allocated.
    strpy(response, "{\"q_results\" : [");
    strcpy(response, str);
    strcpy(response, "] }\0"); //Wrap up and terminate.

    bson_free(str); //Free up str.

    if(mongoc_cursor_error(cursor, &error)) {
      ngx_mg_req_error = malloc(sizeof(error_s));
      ngx_mg_req_error->error_msg = (ngx_str_t)ngx_string("{ \"ok\" : false, \"reason\" : \"There was a problem with the Mongo Cursor.\" }");
      ngx_mg_req_error->error_code = NGX_HTTP_INTERNAL_SERVER_ERROR;
      return NULL;
    }

    bson_destroy(&query);
    mongoc_cursor_destroy(cursor);
    mongoc_collection_destroy(collection);

    ngx_memzero(cv, sizeof(ngx_http_complex_value_t));

    cv->value.len = strlen(response);
    cv->value.data = response;

    return 1;
}

//Handle posting to Mongo.
static char *
ngx_http_mg_handle_post_request(ngx_http_request_t* r, ngx_http_mg_conf_t* mgcf, ngx_http_complex_value_t* cv, char* errormsg) {
    char uri*

    //Mongoc related vars
    mongo_client_t *client;
    mongoc_collection_t *collection;
    mongoc_cursor_t *cursor;

    //Dealing with bson here.
    bson_error_t error;
    const bson_t *doc;
    bson_t doc_response = BSON_INITIALIZER;
    bson_t query;

    //Init our client and connect
    mongoc_init();

    uri = build_conn_uri(mgcf->mongo_server_addr.data, mgcf->mongo_user.data, mgcf->mongo_pass.data);
    client = mongoc_client_new(uri); //Connect to db. Let's assume for the moment all actions require connection.

    if(!client) {
      ngx_mg_req_error = malloc(sizeof(error_s));
      ngx_mg_req_error->error_msg = (ngx_str_t)ngx_string("{ \"ok\" : false, \"reason\" : \"Could not authenticate." }");
      ngx_mg_req_error->error_code = NGX_HTTP_UNAUTHORIZED;
      return NULL;
    }

    //https://github.com/mongodb/mongo-c-driver/blob/master/doc/mongoc_collection_find.txt
    collection = mongoc_client_get_collection(client, mgcf->mongo_database.data, mgcf->mongo_collection.data); //Get our requested collection

    //Also a 500
    if(!collection) {
      ngx_mg_req_error = malloc(sizeof(error_s));
      ngx_mg_req_error->error_msg = (ngx_str_t)ngx_string("{ \"ok\" : false, \"reason\" : \"Unable to get collection.\" }");
      ngx_mg_req_error->error_code = NGX_HTTP_INTERNAL_SERVER_ERROR;
      return NULL;
    }

    return 1;
}

static char *
build_conn_uri(char* addr, char* username, char* pass)
{
     return bson_strdup_printf("mongodb://%s:%s@%s:27017",
                                      pass,
                                      username,
                                      addr);
}

/**
* Appends the correct bson type to the passed in bson object: used during json parsing, called for each
* individual key \ value pair.
* @param b: The existing bson to append the new object to.
* @param val: The value of the key\value pair parsed (void).
* @param key: The character key for the key\value pair passed.
* @param r: The apache request (used for apr memory allocation).
* @return void: Result is appended to passed in bson.
*/
static void json_key_to_bson_key (bson *b, void *val,const char *key,request_rec *r) {
  // Because the mongo C driver currently doesn't drop out invalid keys in an insert,
  // We have to destroy the k: v pair during a POST completely.
  if (strspn("$",key) && r->method_number == M_POST)
    return;

  // Determine the type of the passed in value and call the correct bson_append function.
  switch (json_object_get_type (val)) {
    case json_type_boolean:
      bson_append_bool (b, key, json_object_get_boolean (val));
      break;
    case json_type_double:
      bson_append_double (b, key, json_object_get_double (val));
      break;
    case json_type_int: {
      if (json_object_get_int64(val) > INT32_MAX)
        bson_append_long (b, key, json_object_get_int64 (val));
      else
        bson_append_int (b, key, json_object_get_int64 (val));
      break;
    }
    case json_type_object: {
      // The json value type has recursive behavior - calling back to the json_to_bson function to parse
      // the values internal to the nested json.
      if (strcmp("_id", key) == 0) {
        append_oid(b, val, key);
        break;
      }
      bson *sub;
      sub = json_to_bson (val, r);
      bson_append_bson(b,key,sub);
      bson_destroy (sub);
      break;
    }
    case json_type_array: {
      // To handle nested arrays.
      int pos;
      bson_append_start_array (b, key);
      for (pos = 0; pos < json_object_array_length (val); pos++) {
        char kk[10];
        sprintf(kk,"%d",pos);
        json_key_to_bson_key ( b, json_object_array_get_idx ( val, pos ), kk, r );
      }
      bson_append_finish_array (b);
      break;
    }
    case json_type_string: {
      const char * const strFromJSON = json_object_get_string(val);
      const char * strVal = strFromJSON;
      if (*strVal == '/' && strVal++) while(*strVal != '\0' && *strVal != '/') strVal++;
      if (*strVal == '/') {
        char * regex = (char*) apr_palloc(r->pool, strVal - strFromJSON - 1);
        for (int i = 1; i < strVal - strFromJSON; i++) *(regex+i-1) = *(strFromJSON+i);
        *(regex + (strVal - strFromJSON) - 1) = '\0';
        bson_append_regex(b, key, regex, ++strVal);
      }
      else bson_append_string (b, key, strFromJSON);
      break;
    }
    default:
      break;
  }
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

