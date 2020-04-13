#include <util/curl/curl-helper.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>
#include <util/dstr.h>
#include "util/base.h"
#include <obs-module.h>
#include <util/platform.h>
#include "showroom.h"
#include <util/threading.h>
struct showroom_mem_struct {
	char *memory;
	size_t size;
};
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static size_t showroom_write_cb(void *contents, size_t size, size_t nmemb,
			      void *userp)
{
	size_t realsize = size * nmemb;
	struct showroom_mem_struct *mem = (struct showroom_mem_struct *)userp;

	mem->memory = realloc(mem->memory, mem->size + realsize + 1);
	if (mem->memory == NULL) {
		blog(LOG_WARNING, "showroom_write_cb: realloc returned NULL");
		return 0;
	}

	memcpy(&(mem->memory[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->memory[mem->size] = 0;

	return realsize;
}
showroom_ingest get_ingest_from_json(char *str)
{
	json_error_t error;
	json_t *root;
	showroom_ingest result;
	root = json_loads(str, JSON_REJECT_DUPLICATES,&error);
	if (!root) {
		return result;
	}
	result.url = json_string_value(json_object_get(root, "streaming_url_rtmp"));
	result.key = json_string_value(json_object_get(root, "streaming_key"));
	//result->url = url;
	//result->key = key;
	return result;
	/*size = json_array_size(root);
	for (i = 0; i < size; i++) {
		json_t *data, *sha, *commit, *message;
		const char *message_text;

		data = json_array_get(root, i);
		if (!json_is_object(data)) {
			
		}
	}*/
}
const char* get_access_key_from_json(char* str)
{
	json_error_t error;
	json_t *root;
	char* result;
	root = json_loads(str, JSON_REJECT_DUPLICATES, &error);
	if (!root) {
		return "";
	}
	result = json_string_value(json_object_get(root, "access_key"));
	return result;

}
const showroom_ingest showroom_get_ingest(const char *server,const char *accessKey)
{	//"PbvUohz75YSMN4krNpCL75SmHph6FNdVFXKND43FojPbujOpB6OD2y3J6Yj8BdHmlx6wWWKPzYk0bYj2hMtbmFZPp7Ve3d4DEPgW9ODLfuQC7acm1hWN5ZwEDRFDmYDA"
	char *config = "";
	char *fileName = obs_module_config_path("showroom_ingests.json");
	if (os_file_exists(fileName)) {
		char *data = os_quick_read_utf8_file(fileName);

		pthread_mutex_lock(&mutex);
		strcpy(config, data);
		pthread_mutex_unlock(&mutex);

		bfree(data);
	}
	//char *accessKey = get_access_key_from_json(config);
	CURL *curl_handle;
	CURLcode res;
	struct showroom_mem_struct chunk;
	struct dstr uri;
	long response_code;
	// find the delimiter in stream key
	//const char *delim = strchr(key, '_');
	/*if (delim == NULL) {
		blog(LOG_WARNING,
		     "younow_get_ingest: delimiter not found in stream key");
		return server;
	}*/

	curl_handle = curl_easy_init();

	chunk.memory = malloc(1); /* will be grown as needed by realloc */
	chunk.size = 0;           /* no data at this point */

	dstr_init(&uri);
	dstr_copy(&uri, server);
	dstr_ncat(&uri, accessKey, strlen(accessKey));
	curl_easy_setopt(curl_handle, CURLOPT_USERPWD, "");
	curl_easy_setopt(curl_handle, CURLOPT_URL, uri.array);
	curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, true);
	curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 3L);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, showroom_write_cb);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
	curl_obs_set_revoke_setting(curl_handle);

#if LIBCURL_VERSION_NUM >= 0x072400
	// A lot of servers don't yet support ALPN
	curl_easy_setopt(curl_handle, CURLOPT_SSL_ENABLE_ALPN, 0);
#endif

	res = curl_easy_perform(curl_handle);
	dstr_free(&uri);
	showroom_ingest ingest;
	ingest.url = "";
	ingest.key = "";
	if (res != CURLE_OK) {
		blog(LOG_WARNING,
		     "showroom_get_ingest: curl_easy_perform() failed: %s",
		     curl_easy_strerror(res));
		curl_easy_cleanup(curl_handle);
		free(chunk.memory);
		return ingest;
	}

	curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
	if (response_code != 200) {
		blog(LOG_WARNING,
		     "showroom_get_ingest: curl_easy_perform() returned code: %ld",
		     response_code);
		curl_easy_cleanup(curl_handle);
		free(chunk.memory);
		return ingest;
	}

	curl_easy_cleanup(curl_handle);

	if (chunk.size == 0) {
		blog(LOG_WARNING,
		     "showroom_get_ingest: curl_easy_perform() returned empty response");
		free(chunk.memory);
		return ingest;
	}
	//current_ingest = (showroom_ingest *)malloc(sizeof(showroom_ingest));
	char* response = strdup(chunk.memory);
	ingest = get_ingest_from_json(response);
	free(chunk.memory);
	//free(ingest);
	return ingest;
}
