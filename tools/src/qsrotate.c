
/**
 * Utilities for the quality of service module mod_qos.
 *
 * Log rotation tool.
 *
 * See http://opensource.adnovum.ch/mod_qos/ for further
 * details.
 *
 * Copyright (C) 2007-2014 Pascal Buchbinder
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 */

static const char revision[] = "$Id: qsrotate.c,v 1.23 2014/01/09 08:13:07 pbuchbinder Exp $";

#include <stdio.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <dirent.h>

#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>

#include <pthread.h>

#include <time.h>
#include <zlib.h>     

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "qs_util.h"

#define BUFSIZE        65536
#define HUGE_STR       1024

/* global variables used by main and support thread */
static int m_force_rotation = 0;
static time_t m_tLogEnd = 0;
static time_t m_tRotation = 86400; /* default are 24h */
static int m_nLogFD = -1;
static int m_generations = -1;
static char *m_file_name = NULL;
static long m_messages = 0;
static char *m_cmd = NULL;
static int m_compress = 0;
static int m_stdout = 0;
static long m_counter = 0;
static long m_limit = 2147483648 - (128 * 1024);
static int m_offset = 0;
static int m_offset_enabled = 0;

static void usage(char *cmd, int man) {
  if(man) {
    //.TH [name of program] [section number] [center footer] [left footer] [center header]
    printf(".TH %s 1 \"%s\" \"mod_qos utilities %s\" \"%s man page\"\n", qs_CMD(cmd), man_date,
	   man_version, cmd);
  }
  printf("\n");
  if(man) {
    printf(".SH NAME\n");
  }
  qs_man_print(man, "%s - a log rotation tool (similar to Apache's rotatelogs).\n", cmd);
  printf("\n");
  if(man) {
    printf(".SH SYNOPSIS\n");
  }
  qs_man_print(man, "%s%s -o <file> [-s <sec> [-t <hours>]] [-f] [-z] [-g <num>] [-u <name>] [-p]\n", man ? "" : "Usage: ", cmd);
  printf("\n");
  if(man) {
    printf(".SH DESCRIPTION\n");
  } else {
    printf("Summary\n");
  }
  qs_man_print(man, "%s reads from stdin (piped log) and writes the data to the provided\n", cmd);
  qs_man_print(man, "file rotating the file after the specified time.\n");
  printf("\n");
  if(man) {
    printf(".SH OPTIONS\n");
  } else {
    printf("Options\n");
  }
  if(man) printf(".TP\n");
  qs_man_print(man, "  -o <file>\n");
  if(man) printf("\n");
  qs_man_print(man, "     Output log file to write the data to (use an absolute path).\n");
  if(man) printf("\n.TP\n");
  qs_man_print(man, "  -s <sec>\n");
  if(man) printf("\n");
  qs_man_print(man, "     Rotation interval in seconds, default are 86400 seconds.\n");
  if(man) printf("\n.TP\n");
  qs_man_print(man, "  -t <hours>\n");
  if(man) printf("\n");
  qs_man_print(man, "     Offset to UTC (enables also DST support), default is 0.\n");
  if(man) printf("\n.TP\n");
  qs_man_print(man, "  -b <bytes>\n");
  if(man) printf("\n");
  qs_man_print(man, "     File size limitation (default are %ld bytes).\n", m_limit);
  if(man) printf("\n.TP\n");
  qs_man_print(man, "  -f\n");
  if(man) printf("\n");
  qs_man_print(man, "     Forced log rotation even no data is written.\n");
  if(man) printf("\n.TP\n");
  qs_man_print(man, "  -z\n");
  if(man) printf("\n");
  qs_man_print(man, "     Compress (gzip) the rotated file.\n");
  if(man) printf("\n.TP\n");
  qs_man_print(man, "  -g <num>\n");
  if(man) printf("\n");
  qs_man_print(man, "     Generations (number of files to keep).\n");
  if(man) printf("\n.TP\n");
  qs_man_print(man, "  -u <name>\n");
  if(man) printf("\n");
  qs_man_print(man, "     Become another user, e.g. www-data.\n");
  if(man) printf("\n.TP\n");
  qs_man_print(man, "  -p\n");
  if(man) printf("\n");
  qs_man_print(man, "     Writes data also to stdout (for piped logging).\n");
  printf("\n");
  if(man) {
    printf(".SH EXAMPLE\n");
  } else {
    printf("Example:\n");
  }
  qs_man_println(man, "  TransferLog \"|%s -f -z -g 3 -o /dest/file -s 86400\"\n", cmd);
  printf("\n");
  qs_man_print(man, "The name of the rotated file will be /dest/filee.YYYYmmddHHMMSS\n");
  qs_man_print(man, "where YYYYmmddHHMMSS is the system time at which the data has been\n");
  qs_man_print(man, "rotated.\n");
  printf("\n");
  if(man) {
    printf(".SH NOTE\n");
  } else {
    printf("Note:\n");
  }
  qs_man_print(man, "  Each %s instance must use an individual file.\n", cmd);
  printf("\n");
  if(man) {
    printf(".SH SEE ALSO\n");
    printf("qsexec(1), qsfilter2(1), qsgeo(1), qsgrep(1), qshead(1), qslog(1), qslogger(1), qspng(1), qssign(1), qstail(1)\n");
    printf(".SH AUTHOR\n");
    printf("Pascal Buchbinder, http://opensource.adnovum.ch/mod_qos/\n");
  } else {
    printf("See http://opensource.adnovum.ch/mod_qos/ for further details.\n");
  }
  if(man) {
    exit(0);
  } else {
    exit(1);
  }
}

static time_t get_now() {
  time_t now = time(NULL);
  if(m_offset_enabled) {
    struct tm lcl = *localtime(&now);
    if(lcl.tm_isdst) {
      now += 3600;
    }
    now += m_offset;
  }
  return now;
}

static int openFile(const char *cmd, const char *file_name) {
  int m_nLogFD = open(file_name, O_WRONLY | O_CREAT | O_APPEND, 0660);
  /* error while opening log file */
  if(m_nLogFD < 0) {
    fprintf(stderr,"[%s]: ERROR, failed to open file <%s>\n", cmd, file_name);
  }
  return m_nLogFD;
}

/**
 * Compress method called by a child process (forked)
 * used to compress the rotated file.
 *
 * @param cmd Command name (used when logging errors)
 * @param arch Path to the file to compress. File gets renamed to <arch>.gz
 */
static void compressThread(const char *cmd, const char *arch) {
  gzFile *outfp;
  int infp;
  char dest[HUGE_STR+20];
  char buf[HUGE_STR];
  int len;
  snprintf(dest, sizeof(dest), "%s.gz", arch);
  /* low prio */
  if(nice(10) == -1) {
    fprintf(stderr, "[%s]: WARNING, failed to change nice value: %s\n", cmd, strerror(errno));
  }
  if((infp = open(arch, O_RDONLY)) == -1) {
    /* failed to open file, can't compress it */
    fprintf(stderr,"[%s]: ERROR, could not open file for compression <%s>\n", cmd, arch);
    return;
  }
  if((outfp = gzopen(dest,"wb")) == NULL) {
    fprintf(stderr,"[%s]: ERROR, could not open file for compression <%s>\n", cmd, dest);
    close(infp);
    return;
  }
  while((len = read(infp, buf, sizeof(buf))) > 0) {
    gzwrite(outfp, buf, len);
  }
  gzclose(outfp);
  close(infp);
  /* done, delete the old file */
  unlink(arch);
}

void sigchild(int signo) {
  pid_t pid;
  int stat;   
  while((pid=waitpid(-1,&stat,WNOHANG)) > 0) {
  }
}

/**
 * Rotates a file
 *
 * @param cmd Command name to be used in log messages
 * @param now
 * @param file_name Name of the file to rotate (rename)
 * @param messages Error message if rotation was not successful
 */
static void rotate(const char *cmd, time_t now,
		   const char *file_name, long *messages) {
  int rc;
  char arch[HUGE_STR+20];
  char tmb[20];
  struct tm *ptr = localtime(&now);
  strftime(tmb, sizeof(tmb), "%Y%m%d%H%M%S", ptr);
  snprintf(arch, sizeof(arch), "%s.%s", file_name, tmb);

  /* set next rotation time */
  m_tLogEnd = ((now / m_tRotation) * m_tRotation) + m_tRotation;
  // reset byte counter
  m_counter = 0;
  
  /* rename current file */
  if(m_nLogFD >= 0) {
    close(m_nLogFD);
    rename(file_name, arch);
  }
  
  /* open new file */
  m_nLogFD = openFile(cmd, file_name);
  if(m_nLogFD < 0) {
    /* opening a new file has failed!
       try to reopen and clear the last file */
    char msg[HUGE_STR];
    snprintf(msg, sizeof(msg), "ERROR while writing to file, %ld messages lost\n", *messages);
    fprintf(stderr,"[%s]: ERROR, while writing to file <%s>\n", cmd, file_name);
    rename(arch,  file_name);
    m_nLogFD = openFile(cmd, file_name);
    if(m_nLogFD > 0) {
      rc = ftruncate(m_nLogFD, 0);
      rc = write(m_nLogFD, msg, strlen(msg));
    }
  } else {
    *messages = 0;
    if(m_compress || (m_generations != -1)) {
      signal(SIGCHLD,sigchild);
      if(fork() == 0) {
	if(m_compress) {
	  compressThread(cmd, arch);
	}
	if(m_generations != -1) {
	  qs_deleteOldFiles(file_name, m_generations);
	}
	exit(0);
      }
    }
  }
}

/**
 * Separate thread which initiates file rotation even no
 * log data is written.
 *
 * @param argv (not used)
 */
static void *forcedRotationThread(void *argv) {
  time_t now;
  time_t n;
  while(1) {
    qs_csLock();
    now = get_now();
    if(now > m_tLogEnd) {
      rotate(m_cmd, now, m_file_name, &m_messages);
    }
    qs_csUnLock();
    now = get_now();
    n = 1 + m_tLogEnd - now;
    sleep(n);
  }
  return NULL;
}

int main(int argc, char **argv) {
  char *username = NULL;
  int rc;
  char buf[BUFSIZE];
  int nRead, nWrite;
  time_t now;

  pthread_attr_t *tha = NULL;
  pthread_t tid;

  char *m_cmd = strrchr(argv[0], '/');

  if(m_cmd == NULL) {
    m_cmd = argv[0];
  } else {
    m_cmd++;
  }
  
  while(argc >= 1) {
    if(strcmp(*argv,"-o") == 0) {
      if (--argc >= 1) {
	m_file_name = *(++argv);
      }
    } else if(strcmp(*argv,"-u") == 0) {
      if (--argc >= 1) {
	username = *(++argv);
      }
    } else if(strcmp(*argv,"-s") == 0) {
      if (--argc >= 1) {
	m_tRotation = atoi(*(++argv));
      } 
    } else if(strcmp(*argv,"-t") == 0) {
      if (--argc >= 1) {
	m_offset = atoi(*(++argv));
	m_offset = m_offset * 3600;
	m_offset_enabled = 1;
      } 
    } else if(strcmp(*argv,"-g") == 0) {
      if (--argc >= 1) {
	m_generations = atoi(*(++argv));
      } 
    } else if(strcmp(*argv,"-b") == 0) {
      if (--argc >= 1) {
	m_limit = atoi(*(++argv));
      } 
    } else if(strcmp(*argv,"-z") == 0) {
      m_compress = 1;
    } else if(strcmp(*argv,"-p") == 0) {
      m_stdout = 1;
    } else if(strcmp(*argv,"-f") == 0) {
      m_force_rotation = 1;
    } else if(strcmp(*argv,"-h") == 0) {
      usage(m_cmd, 0);
    } else if(strcmp(*argv,"--help") == 0) {
      usage(m_cmd, 0);
    } else if(strcmp(*argv,"-?") == 0) {
      usage(m_cmd, 0);
    } else if(strcmp(*argv,"--man") == 0) {
      usage(m_cmd, 1);
    }

    argc--;
    argv++;
  }

  if(m_file_name == NULL) usage(m_cmd, 0);
  if(m_limit < (1024 * 1024)) usage(m_cmd, 0);

  if(username && getuid() == 0) {
    struct passwd *pwd = getpwnam(username);
    uid_t uid, gid;
    if(pwd == NULL) {
      fprintf(stderr,"[%s]: ERROR, unknown user id %s\n", m_cmd, username);
      exit(1);
    }
    uid = pwd->pw_uid;
    gid = pwd->pw_gid;
    setgid(gid);
    setuid(uid);
    if(getuid() != uid) {
      fprintf(stderr,"[%s]: ERROR, setuid failed (%s,%d)\n", m_cmd, username, uid);
      exit(1);
    }
    if(getgid() != gid) {
      fprintf(stderr,"[%s]: ERROR, setgid failed (%d)\n", m_cmd, gid);
      exit(1);
    }
  }
  
  /* set next rotation time */
  now = get_now();
  m_tLogEnd = ((now / m_tRotation) * m_tRotation) + m_tRotation;
  /* open file */
  m_nLogFD = openFile(m_cmd, m_file_name);
  if(m_nLogFD < 0) {
    /* startup did not success */
    exit(2);
  }

  if(m_force_rotation) {
    qs_csInitLock();
    pthread_create(&tid, tha, forcedRotationThread, NULL);
  }

  for(;;) {
    nRead = read(0, buf, sizeof buf);
    if(nRead == 0) exit(3);
    if(nRead < 0) if(errno != EINTR) exit(4);
    if(m_force_rotation) {
      qs_csLock();
    }
    m_counter += nRead;
    now = get_now();
    /* write data if we have a file handle (else continue but drop log data,
       re-try to open the file at next rotation time) */
    if(m_nLogFD >= 0) {
      do {
	nWrite = write(m_nLogFD, buf, nRead);
	if(m_stdout) {
	  printf("%.*s", nRead, buf);
	}
      } while (nWrite < 0 && errno == EINTR);
    }
    if(nWrite != nRead) {
      m_messages++;
      if(m_nLogFD >= 0) {
	char msg[HUGE_STR];
	snprintf(msg, sizeof(msg), "ERROR while writing to file, %ld messages lost\n", m_messages);
	/* error while writing data, try to delete the old file and continue ... */
	rc = ftruncate(m_nLogFD, 0);
	rc = write(m_nLogFD, msg, strlen(msg));
      }
    } else {
      m_messages++;
    }
    if((now > m_tLogEnd) || (m_counter > m_limit)) {
      /* rotate! */
      rotate(m_cmd, now, m_file_name, &m_messages);
    }
    if(m_force_rotation) {
      qs_csUnLock();
    }
  }
  return 0;
}
