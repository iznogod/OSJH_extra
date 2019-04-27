#ifndef _GSC_CUSTOM_MYSQL_HPP_
#define _GSC_CUSTOM_MYSQL_HPP_

#include "shared.hpp"

void mysql_handle_result_callbacks(void);

void gsc_mysqla_create_entity_query(int num);
void gsc_mysqla_create_level_query(void);
void gsc_mysqla_get_done_list(void);
void gsc_mysqla_initializer(void);
void gsc_mysqla_ondisconnect(int num);

void gsc_mysqls_get_existing_connection(void);
void gsc_mysqls_real_connect(void);
void gsc_mysqls_close_connection(void);
void gsc_mysqls_query(void);
void gsc_mysqls_errno(void);
void gsc_mysqls_error(void);
void gsc_mysqls_affected_rows(void);
void gsc_mysqls_num_rows(void);
void gsc_mysqls_num_fields(void);
void gsc_mysqls_field_seek(void);
void gsc_mysqls_fetch_field(void);
void gsc_mysqls_real_escape_string(void);

#endif
