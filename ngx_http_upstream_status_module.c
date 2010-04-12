#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


extern ngx_module_t        ngx_http_upstream_module;

static char *ngx_http_upstream_status(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

static ngx_command_t  ngx_http_upstream_status_commands[] = {

    { ngx_string("upstream_status"),
      NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS,
      ngx_http_upstream_status,
      0,
      0,
      NULL },

      ngx_null_command
};

static ngx_http_module_t  ngx_http_upstream_status_module_ctx = {
    NULL,                          /* preconfiguration */
    NULL,                          /* postconfiguration */

    NULL,                          /* create main configuration */
    NULL,                          /* init main configuration */

    NULL,                          /* create server configuration */
    NULL,                          /* merge server configuration */

    NULL,                          /* create location configuration */
    NULL                           /* merge location configuration */
};


static char response_content_type[] = "text/html";

ngx_module_t  ngx_http_upstream_status_module = {
    NGX_MODULE_V1,
    &ngx_http_upstream_status_module_ctx, /* module context */
    ngx_http_upstream_status_commands,   /* module directives */
    NGX_HTTP_MODULE,               /* module type */
    NULL,                          /* init master */
    NULL,                          /* init module */
    NULL,                          /* init process */
    NULL,                          /* init thread */
    NULL,                          /* exit thread */
    NULL,                          /* exit process */
    NULL,                          /* exit master */
    NGX_MODULE_V1_PADDING
};

static ngx_chain_t *
append_buf_to_chain(ngx_pool_t *pool, ngx_chain_t *ch, ngx_buf_t *buf)
{
    ngx_chain_t *retval;

    retval = ngx_pcalloc(pool, sizeof(ngx_chain_t));
    if(retval != NULL)
    {
        retval->next = NULL;
        retval->buf = buf;

        retval->buf->last_buf = 1;

        ch->buf->last_buf = 0; /* unmark last thing in chain as last*/
        ch->next = retval;
        return retval;
    }
    return NULL;
}

static ngx_buf_t *
str_to_buf(ngx_pool_t *pool, char *str)
{
    ngx_buf_t *buf;
    u_char *u_str;

    u_str = (u_char*) str;
    buf = ngx_pcalloc(pool, sizeof(ngx_buf_t));

    if(buf != NULL)
    {
        buf->pos = u_str;
        buf->last = u_str + strlen(str);
        buf->memory = 1;
        buf->last_buf = 1;

        return buf;
    }
    return NULL;
}

static ngx_chain_t *
append_str_to_chain(ngx_pool_t *pool, ngx_chain_t *ch, char *str)
{
    ngx_buf_t *buf;
    buf = str_to_buf(pool, str);

    if(buf != NULL)
    {
        return append_buf_to_chain(pool, ch, buf);
    }
    return NULL;
}

size_t
chain_total_len(ngx_chain_t *ch)
{
    size_t retval = 0;

    while(1) {
        retval += ngx_buf_size(ch->buf);

        if(ch->buf->last_buf == 1)
            break;

        ch = ch->next;
    }

    return retval;
}

static void
ngx_http_upstream_status_writer(ngx_http_upstream_srv_conf_t *uscfp, ngx_http_request_t *r, ngx_chain_t **out)
{
    ngx_log_t                       *log;
    ngx_uint_t                      i;
    ngx_uint_t                      is_upstream_rr;
    ngx_uint_t                      up_down;
    ngx_http_upstream_peer_t        *upstream_set;
    ngx_http_upstream_rr_peers_t    *upstream_round_robin_set;
    ngx_http_upstream_rr_peer_t     *upstream_round_robin_peer;
    ngx_buf_t                       *buf;

    log = r->connection->log;

    ngx_log_error(NGX_LOG_DEBUG, log, 0,
               "service: \"%V\"", &(uscfp->host) );

    buf = ngx_create_temp_buf(r->pool, 200 + 2*ngx_strlen(&(uscfp->host)) );
    buf->last = ngx_sprintf(buf->pos, "<table id=\"%V\" border=\"1\" class=\"vip-group\"><caption>%V</caption>", &(uscfp->host) , &(uscfp->host) );
    *out = append_buf_to_chain(r->pool, *out, buf);

    upstream_set = &(uscfp->peer);

    /* Crude check to see if upstream type is round robin,
       else we cannot interpred upstream_set->data */
    is_upstream_rr = (upstream_set->init == ngx_http_upstream_init_round_robin_peer);
    ngx_log_error(NGX_LOG_DEBUG, log, 0,
               "is %V upstream: %d", &(uscfp->host), is_upstream_rr);


    if(is_upstream_rr) {
        upstream_round_robin_set = upstream_set->data;
        for (i = 0; i < upstream_round_robin_set->number; i++) {
            *out = append_str_to_chain(r->pool, *out, "<tr>");

            upstream_round_robin_peer = &(upstream_round_robin_set->peer[i]);
            up_down = upstream_round_robin_peer->fails < upstream_round_robin_peer->max_fails;
            ngx_log_error(NGX_LOG_DEBUG, log, 0,
                       "reals: %V %d", &(upstream_round_robin_peer->name), up_down );

            buf = ngx_create_temp_buf(r->pool, 20 + ngx_strlen(&(upstream_round_robin_peer->name)) );
            buf->last = ngx_sprintf(buf->pos, "<td>%V</td>",  &(upstream_round_robin_peer->name) );
            *out = append_buf_to_chain(r->pool, *out, buf);

            buf = ngx_create_temp_buf(r->pool, 100 );
            buf->last = ngx_sprintf(buf->pos, "<td class=\"server-%s\">%s</td>", (up_down ? "up" : "down"), (up_down ? "up" : "down") );
            *out = append_buf_to_chain(r->pool, *out, buf);

            *out = append_str_to_chain(r->pool, *out, "</tr>");
        }
    }
    *out = append_str_to_chain(r->pool, *out, "</table>");
}

static ngx_int_t
ngx_http_upstream_status_handler(ngx_http_request_t *r)
{
    ngx_uint_t                      i;
    ngx_int_t                       rc;
    ngx_chain_t                     out, *next;
    ngx_http_upstream_srv_conf_t    **uscfp;
    ngx_http_upstream_main_conf_t   *umcf;
    ngx_log_t                       *log;


    log = r->connection->log;

    out.buf = str_to_buf(r->pool, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\"\n\
   \"http://www.w3.org/TR/html4/strict.dtd\">\n");
    out.next = NULL;
    next = &out;

    next = append_str_to_chain(r->pool, next, "<html><head><title>upstream status</title></head><body>");

    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD))) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    rc = ngx_http_discard_request_body(r);

    if (rc != NGX_OK) {
        return rc;
    }

    umcf = ngx_http_get_module_main_conf(r, ngx_http_upstream_module);

    uscfp = umcf->upstreams.elts;

    for (i = 0; i < umcf->upstreams.nelts; i++) {
        ngx_http_upstream_status_writer(uscfp[i], r, &next);
    }

    append_str_to_chain(r->pool, next, "</body>");

    r->headers_out.content_type.len = sizeof(response_content_type) - 1;
    r->headers_out.content_type.data = (u_char *) response_content_type;

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = chain_total_len(&out);

    ngx_log_error(NGX_LOG_DEBUG, log, 0,
               "ob: %d", chain_total_len(&out) );

    rc = ngx_http_send_header(r);

    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }
    return ngx_http_output_filter(r, &out);
}


static char *
ngx_http_upstream_status(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_upstream_status_handler;

    return NGX_CONF_OK;
}


/*
 * vim: et ts=4 sw=4
 */
