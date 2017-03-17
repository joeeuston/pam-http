// standard stuff
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// pam stuff
#include <security/pam_modules.h>

// libcurl
#include <curl/curl.h>

/* expected hook */
PAM_EXTERN int pam_sm_setcred( pam_handle_t *pamh, int flags, int argc, const char **argv ) {
	return PAM_SUCCESS;
}

PAM_EXTERN int pam_sm_acct_mgmt(pam_handle_t *pamh, int flags, int argc, const char **argv) {
	printf("Acct mgmt\n");
	return PAM_SUCCESS;
}

/*
 * Makes getting arguments easier. Accepted arguments are of the form: name=value
 *
 * @param pName- name of the argument to get
 * @param argc- number of total arguments
 * @param argv- arguments
 * @return Pointer to value or NULL
 */
static const char* getArg(const char* pName, int argc, const char** argv) {
	int len = strlen(pName);
	int i;

	for (i = 0; i < argc; i++) {
		if (strncmp(pName, argv[i], len) == 0 && argv[i][len] == '=') {
			// only give the part url part (after the equals sign)
			return argv[i] + len + 1;
		}
	}
	return 0;
}

/*
 * Function to handle stuff from HTTP response.
 *
 * @param buf- Raw buffer from libcurl.
 * @param len- number of indices
 * @param size- size of each index
 * @param userdata- any extra user data needed
 * @return Number of bytes actually handled. If different from len * size, curl will throw an error
 */
static int writeFn(void* buf, size_t len, size_t size, void* userdata) {
	fprintf(stderr, "%.*s", len, (char*)userdata);
	return len * size;
}

static int getUrl(const char* pUrl, const char* pClient, const char* pUsername, const char* pPassword, const char* pCaFile) {
	fprintf(stderr, "Starting Auth\n");

	CURL* pCurl = curl_easy_init();
	int res = -1;

	char* pUserPass;
	const char* dataFormat = "grant_type=password&username=%s&password=%s";
	int len = strlen(pUsername) + strlen(pPassword) + strlen(dataFormat) + 1; // : separator & trailing null

	if (!pCurl) {
		return 0;
	}

	pUserPass = malloc(len);
	sprintf(pUserPass, dataFormat, pUsername, pPassword);

	char* pAuthHeader;
	char* pAuthHeaderLine = "Authorization: Basic %s";
	int authLen = strlen(pAuthHeaderLine) + strlen(pClient) + 1;
	pAuthHeader = malloc(authLen);
	sprintf(pAuthHeader, pAuthHeaderLine, pClient);

	// create client auth header options
	struct curl_slist *list = NULL;
	list = curl_slist_append(list, pAuthHeader);

	//set curl to do POST

	fprintf(stderr, "Building Curl\n");
	fprintf(stderr, "pUserPass:%s\n", pUserPass);
	fprintf(stderr, "pAuthHeader:%s\n", pAuthHeader);

	curl_easy_setopt(pCurl, CURLOPT_URL, pUrl);
	curl_easy_setopt(pCurl, CURLOPT_POST, 1);
	curl_easy_setopt(pCurl, CURLOPT_POSTFIELDS, pUserPass);
	curl_easy_setopt(pCurl, CURLOPT_HTTPHEADER, list);

	curl_easy_setopt(pCurl, CURLOPT_WRITEFUNCTION, writeFn);
	curl_easy_setopt(pCurl, CURLOPT_NOPROGRESS, 1); // we don't care about progress
	curl_easy_setopt(pCurl, CURLOPT_FAILONERROR, 1);
	// we don't want to leave our user waiting at the login prompt forever
	curl_easy_setopt(pCurl, CURLOPT_TIMEOUT, 1);

	// SSL needs 16k of random stuff. We'll give it some space in RAM.
	curl_easy_setopt(pCurl, CURLOPT_RANDOM_FILE, "/dev/urandom");
	curl_easy_setopt(pCurl, CURLOPT_SSL_VERIFYPEER, 0);
	curl_easy_setopt(pCurl, CURLOPT_SSL_VERIFYHOST, 2);
	curl_easy_setopt(pCurl, CURLOPT_USE_SSL, CURLUSESSL_ALL);

	// synchronous, but we don't really care
	res = curl_easy_perform(pCurl);

	memset(pUserPass, '\0', len);
	free(pUserPass);
	free(pAuthHeader);
	curl_easy_cleanup(pCurl);

	fprintf(stderr, "Res: %d\n", res);

	return res;
}

/* expected hook, this is where custom stuff happens */
PAM_EXTERN int pam_sm_authenticate(pam_handle_t* pamh, int flags, int argc, const char **argv) {
	int ret = 0;

	const char* pUsername = NULL;
	const char* pUrl = NULL;
	const char* pCaFile = NULL;
	const char* pClient = NULL;

	struct pam_message msg;
	struct pam_conv* pItem;
	struct pam_response* pResp;
	const struct pam_message* pMsg = &msg;

	msg.msg_style = PAM_PROMPT_ECHO_OFF;
	msg.msg = "MIS_ID: ";

	fprintf(stderr, "Attempting Auth for Ping Fed\n"); //Debug line

	if (pam_get_user(pamh, &pUsername, NULL) != PAM_SUCCESS) {
		fprintf(stderr, "Getting User failed\n");
		return PAM_AUTH_ERR;
	}

	pUrl = getArg("url", argc, argv);
	if (!pUrl) {
		fprintf(stderr, "Getting url arg failed\n");
		return PAM_AUTH_ERR;
	}
	fprintf(stderr, "pUrl:%s\n", pUrl);

	pClient = getArg("client", argc, argv);
	if (!pClient) {
		fprintf(stderr, "Getting client arg failed\n");
		return PAM_AUTH_ERR;
	}
	fprintf(stderr, "pClient:%s\n", pClient);

	pCaFile = getArg("cafile", argc, argv);
	if (pam_get_item(pamh, PAM_CONV, (const void**)&pItem) != PAM_SUCCESS || !pItem) {
		fprintf(stderr, "Couldn't get pam_conv\n");
		return PAM_AUTH_ERR;
	}
	fprintf(stderr, "pCaFile:%s\n", pCaFile);

	pItem->conv(1, &pMsg, &pResp, pItem->appdata_ptr);

	ret = PAM_SUCCESS;

	if (getUrl(pUrl, pClient, pUsername, pResp[0].resp, pCaFile) != 0) {
		ret = PAM_AUTH_ERR;
	}

	memset(pResp[0].resp, 0, strlen(pResp[0].resp));
	free(pResp);

	return ret;
}
