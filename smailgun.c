/*
 * -----------------------------  smailgun.c  -------------------------------
 *
 * Copyright (c) 2015, Yorick de Wid <yorick17 at outlook dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Simple tree structure. It is a btree but not a binary search tree.
 */

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdarg.h>
#include <syslog.h>
#include <string.h>
#include <curl/curl.h>

#define VERSION "0.1"

#ifndef CONFIGURATION_FILE
#define CONFIGURATION_FILE "/etc/smailgun/smailgun.conf"
#endif

#ifndef LOG_FILE
#define LOG_FILE "/var/log/smailgun.log"
#endif

#ifndef BUF_SZ
#define BUF_SZ 1024
#endif

#ifndef MAXARGS
#define MAXARGS 1024
#endif

int have_from = 0;
#ifdef HASTO_OPTION
int have_to = 0;
#endif
int minus_t = 0;
int minus_v = 0;
int override_from = 0;
int rewrite_domain = 0;

char *api = NULL;
char *domain = NULL;
char *from = NULL;
char *endpoint = "https://api.mailgun.net/v3/%s/messages";
char *minus_f = NULL;
char *minus_F = NULL;
char *prog = NULL;
char *root = NULL;
char *uad = NULL;
char *config_file = NULL;

int log_level = 1;
int have_to = 0;
int have_date = 0;
int minuserid = 0;//MAXSYSUID+1;

struct string_list {
	char *string;
	struct string_list *next;
};

typedef struct string_list headers_t;
typedef struct string_list rcpt_t;

headers_t headers, *ht;
rcpt_t rcpt_list, *rt;

/*
 * strndup() - Duplicate a string.
 */
char *_strndup(char const *s, size_t n) {
	char *str = malloc(n + 1);
	if (!str)
		return NULL;
	str[n] = '\0';
	return memcpy(str, s, n);
}

/*
 * _basename() -- Return last element of path
 */
char *_basename(char *str) {
	char *p;

	p = strrchr(str, '/');
	if (!p) {
		p = str;
	}

	return strdup(p);
}

/*
 * log_event() -- Write event to syslog (or log file if defined)
 */
void log_event(int priority, char *format, ...) {
	char buf[(BUF_SZ + 1)];
	FILE *fp;
	va_list ap;

	va_start(ap, format);
	vsnprintf(buf, BUF_SZ, format, ap);
	va_end(ap);

	if ((fp = fopen(LOG_FILE, "a")) != (FILE *)NULL) {
		fprintf(fp, "%s\n", buf);
		fclose(fp);
	} else {
		fprintf(stderr, "Cannot write to " LOG_FILE "\n");
	}
}

/*
 * die() -- Write error message, dead.letter and exit
*/
void die(char *format, ...) {
	char buf[(BUF_SZ + 1)];
	va_list ap;

	va_start(ap, format);
	vsnprintf(buf, BUF_SZ, format, ap);
	va_end(ap);

	fprintf(stderr, "%s: %s\n", prog, buf);
	log_event(LOG_ERR, "%s", buf);

	/* Send message to dead.letter */
	// dead_letter();

	exit(1);
}

/*
rcpt_save() -- Store entry into RCPT list
*/
void rcpt_save(char *str) {
	char *p;

# if 1
	/* Horrible botch for group stuff */
	p = str;
	while(*p)
		p++;

	if(*--p == ';') {
		return;
	}
#endif

#if 1
	(void)fprintf(stderr, "*** rcpt_save(): str = [%s]\n", str);
#endif

	/* Ignore missing usernames */
	if(*str == '\0') {
		return;
	}

	if((rt->string = strdup(str)) == (char *)NULL) {
		die("rcpt_save() -- strdup() failed");
	}

	rt->next = (rcpt_t *)malloc(sizeof(rcpt_t));
	if(rt->next == (rcpt_t *)NULL) {
		die("rcpt_save() -- malloc() failed");
	}
	rt = rt->next;

	rt->next = (rcpt_t *)NULL;
}

/*
 * rcpt_parse() -- Break To|Cc|Bcc into individual addresses
 */
void rcpt_parse(char *str) {
	int in_quotes = 0, got_addr = 0;
	char *p, *q, *r;

#if 1
	fprintf(stdout, "*** rcpt_parse(): str = [%s]\n", str);
#endif

	if ((p = strdup(str)) == (char *)NULL) {
		die("rcpt_parse(): strdup() failed");
	}
	q = p;

	/* Replace <CR>, <LF> and <TAB> */
	while(*q) {
		switch(*q) {
			case '\t':
			case '\n':
			case '\r':
					*q = ' ';
		}
		q++;
	}
	q = p;

#if 1
	fprintf(stdout, "*** rcpt_parse(): q = [%s]\n", q);
#endif

	r = q;
	while (*q) {
		if (*q == '"') {
			in_quotes = (in_quotes ? 0 : 1);
		}

		/* End of string? */
		if (*(q + 1) == '\0') {
			got_addr = 1;
		}

		/* End of address? */
		if ((*q == ',') && (in_quotes == 0)) {
			got_addr = 1;

			*q = '\0';
		}

		if (got_addr) {
			while (*r && isspace(*r))
				r++;

			puts(r);
			//rcpt_save(addr_parse(r));
			r = (q + 1);
#if 1
			fprintf(stdout, "*** rcpt_parse(): r = [%s]\n", r);
#endif
			got_addr = 0;
		}
		q++;
	}
	free(p);
}

/*
 * header_save() -- Store entry into header list
 */
void header_save(char *str) {
	char *p;

#if 1
	fprintf(stdout, "header_save(): str = [%s]\n", str);
#endif

	if ((p = strdup(str)) == (char *)NULL) {
		die("header_save() -- strdup() failed");
	}
	puts("p");
	puts(p);
	ht->string = p;

	if (strncasecmp(ht->string, "From:", 5) == 0) {
		/* Hack check for NULL From: line */
		if (*(p + 6) == '\0') {
			return;
		}

#ifdef REWRITE_DOMAIN
		if (override_from == 1) {
			uad = from_strip(ht->string);
		} else {
			return;
		}
#endif
		have_from = 1;
	} else if(strncasecmp(ht->string, "To:" ,3) == 0) {
		have_to = 1;
	} else if(strncasecmp(ht->string, "Date:", 5) == 0) {
		have_date = 1;
	}

	if (minus_t) {
		/* Need to figure out recipients from the e-mail */
		if(strncasecmp(ht->string, "To:", 3) == 0) {
			p = (ht->string + 3);
			rcpt_parse(p);
		} else if(strncasecmp(ht->string, "Bcc:", 4) == 0) {
			p = (ht->string + 4);
			//rcpt_parse(p);
           /* Undo adding the header to the list: */
           free(ht->string);
           ht->string = NULL;
           return;
		} else if(strncasecmp(ht->string, "CC:", 3) == 0) {
			p = (ht->string + 3);
			//rcpt_parse(p);
		}
	}

#if 1
	fprintf(stdout, "header_save(): ht->string = [%s]\n", ht->string);
#endif

	ht->next = (headers_t *)malloc(sizeof(headers_t));
	if (ht->next == (headers_t *)NULL) {
		die("header_save() -- malloc() failed");
	}
	ht = ht->next;

	ht->next = (headers_t *)NULL;
}

/*
 * header_parse() -- Break headers into seperate entries
 */
void header_parse(FILE *stream) {
	size_t size = BUF_SZ, len = 0;
	char *p = (char *)NULL, *q;
	int in_header = 1;
	char l = '\0';
	int c;

	while (in_header && ((c = fgetc(stream)) != EOF)) {
		/* Must have space for up to two more characters, since we
			may need to insert a '\r' */
		if ((p == (char *)NULL) || (len >= (size - 1))) {
			size += BUF_SZ;

			p = (char *)realloc(p, (size * sizeof(char)));
			if (p == (char *)NULL) {
				die("header_parse() -- realloc() failed");
			}
			q = (p + len);
		}
		len++;

		if (l == '\n') {
			switch(c) {
				case ' ':
				case '\t':
						/* Must insert '\r' before '\n's embedded in header
						   fields otherwise qmail won't accept our mail
						   because a bare '\n' violates some RFC */

						*(q - 1) = '\r';	/* Replace previous \n with \r */
						*q++ = '\n';		/* Insert \n */
						len++;

						break;

				case '\n':
						in_header = 0;

				default:
						*q = '\0';
						if((q = strrchr(p, '\n'))) {
							*q = '\0';
						}
						header_save(p);

						q = p;
						len = 0;
			}
		}
		*q++ = c;

		l = c;
	}
	if (in_header) {
		if (l == '\n') {
			switch(c) {
				case ' ':
				case '\t':
						/* Must insert '\r' before '\n's embedded in header
						   fields otherwise qmail won't accept our mail
						   because a bare '\n' violates some RFC */

						*(q - 1) = '\r';	/* Replace previous \n with \r */
						*q++ = '\n';		/* Insert \n */
						len++;

						break;

				case '\n':
						in_header = 0;

				default:
						*q = '\0';
						if((q = strrchr(p, '\n'))) {
							*q = '\0';
						}
						header_save(p);

						q = p;
						len = 0;
			}
		}
	}
	free(p);
}

/*
 * This is much like strtok, but does not modify the string
 * argument.
 * Args:
 * 	char **s:
 * 		Address of the pointer to the string we are looking at.
 * 	const char *delim:
 * 		The set of delimiters.
 * Return value:
 *	The first token, copied by strndup (caller have to free it),
 * 	if a token is found, or NULL if isn't (os strndup fails)
 * 	*s points to the rest of the string
 */
char *firsttok(char **s, const char *delim) {
	char *tok;
	char *rest;
	rest = strpbrk(*s, delim);
	if (!rest) {
		return NULL;
	}

	tok = _strndup(*s,rest-(*s));
	if (!tok) {
		die("firsttok() -- strndup() failed");
	}
	*s = rest + 1;
	return tok;
}

/*
 * read_config() -- Open and parse config file and extract values of variables
 */
int read_config() {
	char buf[(BUF_SZ + 1)], *p, *q, *r;
	FILE *fp;

	if (config_file == (char *)NULL) {
		config_file = strdup(CONFIGURATION_FILE);
		if (config_file == (char *)NULL) {
			die("read_config() -- strdup() failed");
		}
	}

	if ((fp = fopen(config_file, "r")) == NULL) {
		return 0;
	}

	while (fgets(buf, sizeof(buf), fp)) {
		char *begin = buf;
		char *rightside;

		/* Make comments invisible */
		if ((p = strchr(buf, '#'))) {
			*p = '\0';
		}

		/* Ignore malformed lines and comments */
		if (strchr(buf, '=') == (char *)NULL)
			continue;

		/* Parse out keywords */
		p = firsttok(&begin, "= \t\n");
		if (p) {
			rightside=begin;
			q = firsttok(&begin, "= \t\n");
		}

		if (p && q) {
			if (strcasecmp(p, "root") == 0) {
				if ((root = strdup(q)) == (char *)NULL) {
					die("read_config() -- strdup() failed");
				}

				if (log_level > 0) {
					log_event(LOG_INFO, "set root=\"%s\"\n", root);
				}
			} else if (strcasecmp(p, "minUserId") == 0) {
				if((r = strdup(q)) == (char *)NULL) {
					die("read_config() -- strdup() failed");
				}

				minuserid = atoi(r);

				if(log_level > 0) {
					log_event(LOG_INFO, "set minUserId=\"%d\"\n", minuserid);
				}
			} else if (strcasecmp(p, "api") == 0) {
				if ((api = strdup(q)) == (char *)NULL) {
					die("read_config() -- strdup() failed");
				}

				if (log_level > 0) {
					log_event(LOG_INFO, "set api=\"%s\"\n", api);
				}
			} else if (strcasecmp(p, "rewriteDomain") == 0) {
				if ((p = strrchr(q, '@'))) {
					domain = strdup(++p);

					log_event(LOG_ERR,
						"set rewriteDomain=\"%s\" is invalid\n", q);
					log_event(LOG_ERR,
						"set rewriteDomain=\"%s\" used\n", domain);
				} else {
					domain = strdup(q);
				}

				if (domain == (char *)NULL) {
					die("read_config() -- strdup() failed");
				}

				rewrite_domain = 1;

				if (log_level > 0) {
					log_event(LOG_INFO, "set rewriteDomain=\"%s\"\n", domain);
				}
			} else if(strcasecmp(p, "fromLineOverride") == 0) {
				if (strcasecmp(q, "yes") == 0) {
					override_from = 1;
				} else {
					override_from = 0;
				}

				if (log_level > 0) {
					log_event(LOG_INFO, "set fromLineOverride=\"%s\"\n",
						override_from ? "True" : "False");
				}
			} else if (strcasecmp(p, "domain") == 0) {
				if ((domain = strdup(q)) == (char *)NULL) {
					die("read_config() -- strdup() failed");
				}

				if (log_level > 0) {
					log_event(LOG_INFO, "set domain=\"%s\"\n", domain);
				}
			} else if (strcasecmp(p, "debug") == 0) {
				if (strcasecmp(q, "yes") == 0) {
					log_level = 1;
				} else {
					log_level = 0;
				}
			} else {
				log_event(LOG_INFO, "unable to set %s=\"%s\"\n", p, q);
			}
			free(p);
			free(q);
		}
	}
	fclose(fp);

	return 1;
}

/*
 * smailgun() -- make the api call to the mailgun service.
 */
int smailgun(char *argv[]) {
	CURL *curl;
	CURLcode res;
	char apifull[41];

	if (!read_config()) {
		log_event(LOG_INFO, "%s not found", config_file);
	}

	if (!api || !domain) {
		die("api or domain not set");
	}

	/* Compose URL */
	char *url = (char *)malloc(strlen(endpoint) + strlen(domain) + 1);
	sprintf(url, endpoint, domain);

	/* Create api auth */
	strcpy(apifull, "api:");
	strncat(apifull, api, 41);
	apifull[40] = '\0';

	printf("domain => %s\n", domain);
	printf("API => %s\n", apifull);

	header_parse(stdin);

	printf("from => %s\n", from);

	return 0;

	/* In windows, this will init the winsock stuff */
	curl_global_init(CURL_GLOBAL_ALL);

	/* get a curl handle */
	curl = curl_easy_init();
	if(curl) {
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
		curl_easy_setopt(curl, CURLOPT_HTTPAUTH, (long)CURLAUTH_ANY);
		curl_easy_setopt(curl, CURLOPT_USERPWD, apifull);

		/* Perform the request, res will get the return code */
		res = curl_easy_perform(curl);

		/* Check for errors */
	 	if(res != CURLE_OK)
			log_event(LOG_INFO, "api call failed: %s", curl_easy_strerror(res));

		/* always cleanup */
		curl_easy_cleanup(curl);
	}

	curl_global_cleanup();
	free(url);
	return 0;
}

/* pae() - Write error message and exit */
void pae(char *format, ...) {
	va_list ap;

	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);

	exit(0);
}

/*
 * parse_options() -- Pull the options out of the command-line
 *	Process them (special-case calls to mailq, etc) and return the rest
 */
char **parse_options(int argc, char *argv[]) {
	static char version[] = VERSION;
	static char *new_argv[MAXARGS];
	int i, j, add, new_argc;

	new_argv[0] = argv[0];
	new_argc = 1;

	if (strcmp(prog, "mailq") == 0) {
		/* Queue state */
		pae("mailq: ignore action\n");
	} else if (strcmp(prog, "newaliases") == 0) {
		/* Rebuild aliases */
		pae("newaliases: ignore action\n");
	}

	i = 1;
	while (i < argc) {
		if (argv[i][0] != '-') {
			new_argv[new_argc++] = argv[i++];
			continue;
		}
		j = 0;

		add = 1;
		while (argv[i][++j] != '\0') {
			switch (argv[i][j]) {
				case 'a':
					switch (argv[i][++j]) {
						case 'u':
							if ((!argv[i][(j + 1)])	&& argv[(i + 1)]) {
								//auth_user = strdup(argv[i+1]);
								puts("API:");
								puts(strdup(argv[i + 1]));
								//if (auth_user == (char *)NULL) {
								//	die("parse_options() -- strdup() failed");
								//}
								add++;
							} else {
								puts(strdup(argv[i] + j + 1));
								//auth_user = strdup(argv[i]+j+1);
								//if (auth_user == (char *)NULL) {
								//	die("parse_options() -- strdup() failed");
								//}
							}
							goto exit;
					}

				case 'b':
					switch (argv[i][++j]) {
						case 'a':	/* ARPANET mode */
							pae("-ba: action ignored\n");
						case 'd':	/* Run as a daemon */
							pae("-bd: action ignored\n");
						case 'i':	/* Initialise aliases */
							pae("-bi: action ignored\n", prog);
						case 'm':	/* Default addr processing */
							continue;
						case 'p':	/* Print mailqueue */
							pae("-bp: action ignored\n", prog);
						case 's':	/* Read SMTP from stdin */
							pae("-bs: action ignored\n");
						case 't':	/* Test mode */
							pae("-bt: action ignored\n");
						case 'v':	/* Verify names only */
							pae("-bv: action ignored\n");
						case 'z':	/* Create freeze file */
							pae("-bz: action ignored\n");
					}

				/* Configfile name */
				case 'C':
					if ((!argv[i][(j + 1)]) && argv[(i + 1)]) {
						config_file = strdup(argv[(i + 1)]);
						if (config_file == (char *)NULL) {
							die("parse_options() -- strdup() failed");
						}
						add++;
					} else {
						config_file = strdup(argv[i]+j+1);
						if (config_file == (char *)NULL) {
							die("parse_options() -- strdup() failed");
						}
					}
					goto exit;

				/* Debug */
				case 'd':
					log_level = 1;
					/* Almost the same thing... */
					minus_v = 1;
					continue;

				/* Insecure channel, don't trust userid */
				case 'E':
					continue;

				case 'R':
					/* Amount of the message to be returned */
					if (!argv[i][j+1]) {
						add++;
						goto exit;
					} else {
						/* Process queue for recipient */
						continue;
					}

				/* Fullname of sender */
				case 'F':
					if ((!argv[i][(j + 1)]) && argv[(i + 1)]) {
						minus_F = strdup(argv[(i + 1)]);
						if (minus_F == (char *)NULL) {
							die("parse_options() -- strdup() failed");
						}
						add++;
					} else {
						minus_F = strdup(argv[i]+j+1);
						if (minus_F == (char *)NULL) {
							die("parse_options() -- strdup() failed");
						}
					}
					goto exit;

				/* Set from/sender address */
				case 'f':

				/* Obsolete -f flag */
				case 'r':
					if ((!argv[i][(j + 1)]) && argv[(i + 1)]) {
						minus_f = strdup(argv[(i + 1)]);
						if (minus_f == (char *)NULL) {
							die("parse_options() -- strdup() failed");
						}
						add++;
					} else {
						minus_f = strdup(argv[i] + j + 1);
						if (minus_f == (char *)NULL) {
							die("parse_options() -- strdup() failed");
						}
					}
					goto exit;

				/* Set hopcount */
				case 'h':
					continue;

				/* Ignore originator in adress list */
				case 'm':
					continue;

				/* Use specified message-id */
				case 'M':
					goto exit;

				/* DSN options */
				case 'N':
					add++;
					goto exit;

				/* No aliasing */
				case 'n':
					continue;

				case 'o':
					switch (argv[i][++j]) {

						/* Alternate aliases file */
						case 'A':
							goto exit;

						/* Delay connections */
						case 'c':
							continue;

						/* Run newaliases if required */
						case 'D':
							pae("-oD: action ignored\n");

						/* Deliver now, in background or queue */
						/* This may warrant a diagnostic for b or q */
						case 'd':
							continue;

						/* Errors: mail, write or none */
						case 'e':
							j++;
							continue;

						/* Set tempfile mode */
						case 'F':
							goto exit;

						/* Save ``From ' lines */
						case 'f':
							continue;

						/* Set group id */
						case 'g':
							goto exit;

						/* Helpfile name */
						case 'H':
							continue;

						/* DATA ends at EOF, not \n.\n */
						case 'i':
							continue;

						/* Log level */
						case 'L':
							goto exit;

						/* Send to me if in the list */
						case 'm':
							continue;

						/* Old headers, spaces between adresses */
						case 'o':
							pae("-oo: action ignored\n");

						/* Queue dir */
						case 'Q':
							goto exit;

						/* Read timeout */
						case 'r':
							goto exit;

						/* Always init the queue */
						case 's':
							continue;

						/* Stats file */
						case 'S':
							goto exit;

						/* Queue timeout */
						case 'T':
							goto exit;

						/* Set timezone */
						case 't':
							goto exit;

						/* Set uid */
						case 'u':
							goto exit;

						/* Set verbose flag */
						case 'v':
							minus_v = 1;
							continue;
					}
					break;

				/* Process the queue [at time] */
				case 'q':
					pae("%s: Mail queue is empty\n", prog);

				/* Read message's To/Cc/Bcc lines */
				case 't':
					minus_t = 1;
					continue;

				/* minus_v (ditto -ov) */
				case 'v':
					minus_v = 1;
					break;

				/* Say version and quit */
				/* Similar as die, but no logging */
				case 'V':
					pae("smailgun %s\n", version);
			}
		}

exit:
		i += add;
	}

	new_argv[new_argc] = NULL;

	if (new_argc <= 1 && !minus_t) {
		pae("%s: no recipients supplied - mail will not be sent\n", prog);
	}

	if (new_argc > 1 && minus_t) {
		pae("%s: recipients with -t option not supported\n", prog);
	}

	return &new_argv[0];
}

int main(int argc, char *argv[]) {
	prog = _basename(argv[0]);

	char **_argv = parse_options(argc, argv);

	return smailgun(_argv);
}
