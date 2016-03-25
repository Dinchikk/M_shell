/* ladsh4.c */

#define GNU SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <linux/types.h>
#include <sys/types.h>
#include <pwd.h>

#define MAX_COMMAND_LEN 250
#define JOB_STATUS_FORMAT "[%d] %-22s %.40s\n"
#define CONSTSIZE 256


struct jobSet {
	struct job * head; /*заголовок списка выполняющихся заданий */
	struct job * fg;   /*текущее высокоприоритетное задание */
};

enum redirectionType { REDIRECT_INPUT, REDIRECT_OVERWRITE, REDIRECT_APPEND };

struct redirectionSpecifier {
	enum redirectionType type;	/* тип переадресации */
	int fd;						/* переадресация fd */
	char * filename;			/* файл, в который будет переадресовано fd */
};

struct childProgram {
	pid_t pid;					/* 0 в случае выхода */
	char ** argv;				/* имя программы и аргументы */
	int numRedirections;		/* элементы в массиве переадресации */
	struct redirectionSpecifier * redirections; /* переадресации ввода-вывода */
	glob_t globResult;			/* результат универсализации параметра */
	int freeGlob;				/* нужно ли освобождать globResult? */
	int isStopped;				/* выполняется ли в данный момент программа?*/
};

struct job {
	int jobId; 					/* номер задания */
	int numProgs; 				/* количество программ в задании */
	int runningProgs; 			/* количество выполняющихся программ */
	char * text; 				/* имя задания */
	char * cmdBuf; 				/* буфер, на который ссылаются различные массивы argv */
	pid_t pgrp; 				/* идентификатор группы процесса для задания */
	struct childProgram * progs; /* массив программ в задании */
	struct job * next; 			/* для отслеживания фоновых команд */
	int stoppedProgs; 			/* количество активных, но приостановленных программ */
};

char WD[1000];
char SHELL[1000];
char *USER;
char *HOME;
int PID_n=0;
unsigned long PID;
unsigned long UID;
char** history;
int his_count = 0;
pid_t fgprogsPID = 0;
int cnt_argc;
char str_cntargc[2];
char** arg_arr;
int stdinFD, stdoutFD;
int status_fg=0;

void itoa_m(int n, char* s)
 {
    int i,j, sign;
    char c;

    if ((sign = n) < 0)
        n = -n;
    i = 0;
    do {
        s[i++] = n % 10 + '0';
    } while ((n /= 10) > 0);
    if (sign < 0)
        s[i++] = '-';
    s[i] = '\0';
    for (i = 0, j = strlen(s)-1; i<j; i++, j--) {
         c = s[i];
         s[i] = s[j];
         s[j] = c;
     }
 }

char** variables_Chg(char** argv)
{
    int i = 0;
    int j, k, pos;
    int flag_chg = 0;
    char** new_argv;
    unsigned long newsize;
    char *tmp_cwd, *sstr;
    char *ret, *tmp, UIDstr[34],PIDstr[34] ;
    sprintf(UIDstr, "%lu", UID);
    sprintf(PIDstr, "%lu", PID);

    while (argv[i] != NULL) i++;
    new_argv = calloc(i + 1, sizeof(char*));
    for(j = 0; j < i; j++)
    {
        new_argv[j] = malloc((strlen(argv[j]) + 2) * sizeof(char));
        k = 0;
        while (argv[j][k] != '\0')
            new_argv[j][k] = argv[j][k++];
        new_argv[j][k] = '\0';
    }
    new_argv[i] = NULL;

    i = 0;
    while (new_argv[i] != NULL)
    {
        if ((new_argv[i][0] == '"') || (new_argv[i][0] == '\''))/* двойная*/
        {
            if (new_argv[i][0] == '"')
                    flag_chg = 1;
            else    flag_chg = 2;

            newsize = strlen(new_argv[i]);
            tmp = calloc(newsize, sizeof(char));
            strcat(tmp, &(new_argv[i][1]));
            if ((tmp[newsize - 2] == '"') || (tmp[newsize - 2] == '\''))
                tmp[newsize - 2] = '\0';
            else tmp[newsize - 1] = '\0';
            new_argv[i][0] = '\0';
            strcat(new_argv[i],tmp);
            free(tmp);
        }
        if(flag_chg != 2)
        {
            if ((ret = strstr(new_argv[i], "${PWD}")))
            {
                pos = ret - new_argv[i];
                while ((ret=strstr((new_argv[i] + pos), "${PWD}")))
                {
                    if ((ret != new_argv[i]) && (*(ret - 1) == '\\'))
                    {
                        k = 0;
                        while (*(ret + k) != '\0') *(ret + k - 1) = *(ret + (k++));
                        *(ret + k - 1) = '\0';
                        pos = ret - new_argv[i] + 1;
                        continue;
                    }
                    newsize = 50;
                    tmp_cwd = malloc(newsize);
                    while (!getcwd(tmp_cwd, newsize) && errno == ERANGE)
                    {
                        newsize += 50;
                        tmp_cwd = realloc(tmp_cwd, newsize);
                    }

                    newsize = strlen(new_argv[i]) + strlen(tmp_cwd) + 3;
                    tmp = calloc(newsize, sizeof(char));

                    strcat(tmp, tmp_cwd);
                    free(tmp_cwd);

                    new_argv[i] = realloc(new_argv[i], newsize * sizeof(char));
                    ret=strstr((new_argv[i] + pos), "${PWD}");
                    ret[0] ='\0';

                    strcat(tmp, &(ret[6]));
                    strcat(new_argv[i], tmp);
                    free(tmp);
                }
            }
            if ((ret = strstr(new_argv[i], "${HOME}")))
            {
                pos = ret - new_argv[i];
                while ((ret=strstr((new_argv[i] + pos), "${HOME}")))
                {
                    if ((ret != new_argv[i]) && (*(ret - 1) == '\\'))
                    {
                        k = 0;
                        while (*(ret + k) != '\0') *(ret + k - 1) = *(ret + (k++));
                        *(ret + k - 1) = '\0';
                        pos = ret - new_argv[i] + 1;
                        continue;
                    }
                    newsize = strlen(new_argv[i]) + strlen(HOME) + 1;
                    tmp = calloc(newsize, sizeof(char));
                    new_argv[i] = realloc(new_argv[i], newsize * sizeof(char));
                    ret=strstr((new_argv[i] + pos), "${HOME}");
                    ret[0] ='\0';
                    strcat(tmp,HOME);
                    strcat(tmp, &(ret[7]));
                    strcat(new_argv[i], tmp);
                    free(tmp);
                }
            }
            if ((ret = strstr(new_argv[i], "${PID}")))
            {
                pos = ret - new_argv[i];
                while ((ret=strstr((new_argv[i] + pos), "${PID}")))
                {
                    if ((ret != new_argv[i]) && (*(ret - 1) == '\\'))
                    {
                        k = 0;
                        while (*(ret + k) != '\0') *(ret + k - 1) = *(ret + (k++));
                        *(ret + k - 1) = '\0';
                        pos = ret - new_argv[i] + 1;
                        continue;
                    }
                    newsize = strlen(new_argv[i]) + strlen(PIDstr) + 1;
                    tmp = calloc(newsize, sizeof(char));
                    new_argv[i] = realloc(new_argv[i], newsize * sizeof(char));
                    ret=strstr((new_argv[i] + pos), "${PID}");
                    ret[0] ='\0';
                    strcat(tmp,PIDstr);
                    strcat(tmp, &(ret[6]));
                    strcat(new_argv[i], tmp);
                    free(tmp);
                }
            }
            if ((ret = strstr(new_argv[i], "${UID}")))
            {
                pos = ret - new_argv[i];
                while ((ret=strstr((new_argv[i] + pos), "${UID}")))
                {
                    if ((ret != new_argv[i]) && (*(ret - 1) == '\\'))
                    {
                        k = 0;
                        while (*(ret + k) != '\0') *(ret + k - 1) = *(ret + (k++));
                        *(ret + k - 1) = '\0';
                        pos = ret - new_argv[i] + 1;
                        continue;
                    }
                    newsize = strlen(new_argv[i]) + strlen(UIDstr) + 1;
                    tmp = calloc(newsize, sizeof(char));
                    new_argv[i] = realloc(new_argv[i], newsize * sizeof(char));
                    ret=strstr((new_argv[i] + pos), "${UID}");
                    ret[0] ='\0';
                    strcat(tmp,UIDstr);
                    strcat(tmp, &(ret[6]));
                    strcat(new_argv[i], tmp);
                    free(tmp);
                }
            }
            if ((ret = strstr(new_argv[i], "${USER}")))
            {
                pos = ret - new_argv[i];
                while ((ret=strstr((new_argv[i] + pos), "${USER}")))
                {
                    if ((ret != new_argv[i]) && (*(ret - 1) == '\\'))
                    {
                        k = 0;
                        while (*(ret + k) != '\0') *(ret + k - 1) = *(ret + (k++));
                        *(ret + k - 1) = '\0';
                        pos = ret - new_argv[i] + 1;
                        continue;
                    }
                    newsize = strlen(new_argv[i]) + strlen(USER) + 1;
                    tmp = calloc(newsize, sizeof(char));
                    new_argv[i] = realloc(new_argv[i], newsize * sizeof(char));
                    ret=strstr((new_argv[i] + pos), "${USER}");
                    ret[0] ='\0';
                    strcat(tmp,USER);
                    strcat(tmp, &(ret[7]));
                    strcat(new_argv[i], tmp);
                    free(tmp);
                }
            }
            if ((ret = strstr(new_argv[i], "${SHELL}")))
            {
                pos = ret - new_argv[i];
                while ((ret=strstr((new_argv[i] + pos), "${SHELL}")))
                {
                    if ((ret != new_argv[i]) && (*(ret - 1) == '\\'))
                    {
                        k = 0;
                        while (*(ret + k) != '\0') *(ret + k - 1) = *(ret + (k++));
                        *(ret + k - 1) = '\0';
                        pos = ret - new_argv[i] + 1;
                        continue;
                    }
                    newsize = strlen(new_argv[i]) + strlen(SHELL) + 1;
                    tmp = calloc(newsize, sizeof(char));
                    new_argv[i] = realloc(new_argv[i], newsize * sizeof(char));
                    ret=strstr((new_argv[i] + pos), "${SHELL}");
                    ret[0] ='\0';
                    strcat(tmp,SHELL);
                    strcat(tmp, &(ret[8]));
                    strcat(new_argv[i], tmp);
                    free(tmp);
                }
            }
            if ((ret = strstr(new_argv[i], "$#")))
            {
                pos = ret - new_argv[i];
                while ((ret=strstr((new_argv[i] + pos), "$#")))
                {
                    if ((ret != new_argv[i]) && (*(ret - 1) == '\\'))
                    {
                        k = 0;
                        while (*(ret + k) != '\0') *(ret + k - 1) = *(ret + (k++));
                        *(ret + k - 1) = '\0';
                        pos = ret - new_argv[i] + 1;
                        continue;
                    }
                    newsize = strlen(new_argv[i]) + 2 + 1;
                    tmp = calloc(newsize, sizeof(char));
                    new_argv[i] = realloc(new_argv[i], newsize * sizeof(char));
                    ret=strstr((new_argv[i] + pos), "$#");
                    ret[0] ='\0';
                    strcat(tmp,str_cntargc);
                    strcat(tmp, &(ret[2]));
                    strcat(new_argv[i], tmp);
                    free(tmp);
                }
            }
            if ((ret = strstr(new_argv[i], "$?")))
            {
                pos = ret - new_argv[i];
                while ((ret=strstr((new_argv[i] + pos), "$?")))
                {
                    if ((ret != new_argv[i]) && (*(ret - 1) == '\\'))
                    {
                        k = 0;
                        while (*(ret + k) != '\0') *(ret + k - 1) = *(ret + (k++));
                        *(ret + k - 1) = '\0';
                        pos = ret - new_argv[i] + 1;
                        continue;
                    }
                    sstr = calloc(10,sizeof(char));
                    itoa_m(status_fg , sstr);
                    newsize = strlen(new_argv[i]) + strlen(sstr) + 1;
                    tmp = calloc(newsize, sizeof(char));
                    new_argv[i] = realloc(new_argv[i], newsize * sizeof(char));
                    ret=strstr((new_argv[i] + pos), "$?");
                    ret[0] ='\0';
                    strcat(tmp,sstr);
                    strcat(tmp, &(ret[2]));
                    strcat(new_argv[i], tmp);
                    free(tmp);
                    free(sstr);
                }
            }
            if ((ret = strstr(new_argv[i], "$0")))
            {
                pos = ret - new_argv[i];
                while ((ret=strstr((new_argv[i] + pos), "$0")))
                {
                    if ((ret != new_argv[i]) && (*(ret - 1) == '\\'))
                    {
                        k = 0;
                        while (*(ret + k) != '\0') *(ret + k - 1) = *(ret + (k++));
                        *(ret + k - 1) = '\0';
                        pos = ret - new_argv[i] + 1;
                        continue;
                    }
                    newsize = strlen(new_argv[i]) + strlen(arg_arr[0]) + 1;
                    tmp = calloc(newsize, sizeof(char));
                    new_argv[i] = realloc(new_argv[i], newsize * sizeof(char));
                    ret=strstr((new_argv[i] + pos), "$0");
                    ret[0] ='\0';
                    strcat(tmp,arg_arr[0]);
                    strcat(tmp, &(ret[2]));
                    strcat(new_argv[i], tmp);
                    free(tmp);
                }
            }
            if ((ret = strstr(new_argv[i], "$1")))
            {
                if(cnt_argc <= 1)
                {
                    fprintf(stderr,"Don't have such arg");
                }else
                {
                    pos = ret - new_argv[i];
                    while ((ret=strstr((new_argv[i] + pos), "$1")))
                    {
                        if ((ret != new_argv[i]) && (*(ret - 1) == '\\'))
                        {
                            k = 0;
                            while (*(ret + k) != '\0') *(ret + k - 1) = *(ret + (k++));
                            *(ret + k - 1) = '\0';
                            pos = ret - new_argv[i] + 1;
                            continue;
                        }
                        newsize = strlen(new_argv[i]) + strlen(arg_arr[1]) + 1;
                        tmp = calloc(newsize, sizeof(char));
                        new_argv[i] = realloc(new_argv[i], newsize * sizeof(char));
                        ret=strstr((new_argv[i] + pos), "$1");
                        ret[0] ='\0';
                        strcat(tmp,arg_arr[1]);
                        strcat(tmp, &(ret[2]));
                        strcat(new_argv[i], tmp);
                        free(tmp);
                    }
                }
            }
            if ((ret = strstr(new_argv[i], "$2")))
            {
                if(cnt_argc <= 2)
                {
                    fprintf(stderr,"Don't have such arg");
                }else
                {
                    pos = ret - new_argv[i];
                    while ((ret=strstr((new_argv[i] + pos), "$2")))
                    {
                        if ((ret != new_argv[i]) && (*(ret - 1) == '\\'))
                        {
                            k = 0;
                            while (*(ret + k) != '\0')
                                *(ret + k - 1) = *(ret + (k++));
                            *(ret + k - 1) = '\0';
                            pos = ret - new_argv[i] + 1;
                            continue;
                        }
                        newsize = strlen(new_argv[i]) + strlen(arg_arr[2]) + 1;
                        tmp = calloc(newsize, sizeof(char));
                        new_argv[i] = realloc(new_argv[i], newsize * sizeof(char));
                        ret=strstr((new_argv[i] + pos), "$2");
                        ret[0] ='\0';
                        strcat(tmp,arg_arr[2]);
                        strcat(tmp, &(ret[2]));
                        strcat(new_argv[i], tmp);
                        free(tmp);
                    }
                }
            }
            if ((ret = strstr(new_argv[i], "$3")))
            {
                if(cnt_argc <= 3)
                {
                    fprintf(stderr,"Don't have such arg");
                }else
                {
                    pos = ret - new_argv[i];
                    while ((ret=strstr((new_argv[i] + pos), "$3")))
                    {
                        if ((ret != new_argv[i]) && (*(ret - 1) == '\\'))
                        {
                            k = 0;
                            while (*(ret + k) != '\0') *(ret + k - 1) = *(ret + (k++));
                            *(ret + k - 1) = '\0';
                            pos = ret - new_argv[i] + 1;
                            continue;
                        }
                        newsize = strlen(new_argv[i]) + strlen(arg_arr[3]) + 1;
                        tmp = calloc(newsize, sizeof(char));
                        new_argv[i] = realloc(new_argv[i], newsize * sizeof(char));
                        ret=strstr((new_argv[i] + pos), "$3");
                        ret[0] ='\0';
                        strcat(tmp,arg_arr[3]);
                        strcat(tmp, &(ret[2]));
                        strcat(new_argv[i], tmp);
                        free(tmp);
                    }
                }
            }
            if ((ret = strstr(new_argv[i], "$4")))
            {
                if(cnt_argc <= 4)
                {
                    fprintf(stderr,"Don't have such arg");
                }else
                {
                    pos = ret - new_argv[i];
                    while ((ret=strstr((new_argv[i] + pos), "$4")))
                    {
                        if ((ret != new_argv[i]) && (*(ret - 1) == '\\'))
                        {
                            k = 0;
                            while (*(ret + k) != '\0') *(ret + k - 1) = *(ret + (k++));
                            *(ret + k - 1) = '\0';
                            pos = ret - new_argv[i] + 1;
                            continue;
                        }
                        newsize = strlen(new_argv[i]) + strlen(arg_arr[4]) + 1;
                        tmp = calloc(newsize, sizeof(char));
                        new_argv[i] = realloc(new_argv[i], newsize * sizeof(char));
                        ret=strstr((new_argv[i] + pos), "$4");
                        ret[0] ='\0';
                        strcat(tmp,arg_arr[4]);
                        strcat(tmp, &(ret[2]));
                        strcat(new_argv[i], tmp);
                        free(tmp);
                    }
                }
            }
            if ((ret = strstr(new_argv[i], "$5")))
            {
                if(cnt_argc <= 5)
                {
                    fprintf(stderr,"Don't have such arg");
                }else
                {
                    pos = ret - new_argv[i];
                    while ((ret=strstr((new_argv[i] + pos), "$5")))
                    {
                        if ((ret != new_argv[i]) && (*(ret - 1) == '\\'))
                        {
                            k = 0;
                            while (*(ret + k) != '\0') *(ret + k - 1) = *(ret + (k++));
                            *(ret + k - 1) = '\0';
                            pos = ret - new_argv[i] + 1;
                            continue;
                        }
                        newsize = strlen(new_argv[i]) + strlen(arg_arr[5]) + 1;
                        tmp = calloc(newsize, sizeof(char));
                        new_argv[i] = realloc(new_argv[i], newsize * sizeof(char));
                        ret=strstr((new_argv[i] + pos), "$5");
                        ret[0] ='\0';
                        strcat(tmp,arg_arr[5]);
                        strcat(tmp, &(ret[2]));
                        strcat(new_argv[i], tmp);
                        free(tmp);
                    }
                }
            }
            if ((ret = strstr(new_argv[i], "$6")))
            {
                if(cnt_argc <= 6)
                {
                    fprintf(stderr,"Don't have such arg");
                }else
                {
                    pos = ret - new_argv[i];
                    while ((ret = strstr((new_argv[i] + pos), "$6")))
                    {
                        if ((ret != new_argv[i]) && (*(ret - 1) == '\\'))
                        {
                            k = 0;
                            while (*(ret + k) != '\0') *(ret + k - 1) = *(ret + (k++));
                            *(ret + k - 1) = '\0';
                            pos = ret - new_argv[i] + 1;
                            continue;
                        }
                        newsize = strlen(new_argv[i]) + strlen(arg_arr[6]) + 1;
                        tmp = calloc(newsize, sizeof(char));
                        new_argv[i] = realloc(new_argv[i], newsize * sizeof(char));
                        ret=strstr((new_argv[i] + pos), "$6");
                        ret[0] ='\0';
                        strcat(tmp,arg_arr[6]);
                        strcat(tmp, &(ret[2]));
                        strcat(new_argv[i], tmp);
                        free(tmp);
                    }
                }
            }
            if ((ret = strstr(new_argv[i], "$7")))
            {
                if(cnt_argc <= 7)
                {
                    fprintf(stderr,"Don't have such arg");
                }else
                {
                    pos = ret - new_argv[i];
                    while ((ret = strstr((new_argv[i] + pos), "$7")))
                    {
                        if ((ret != new_argv[i]) && (*(ret - 1) == '\\'))
                        {
                            k = 0;
                            while (*(ret + k) != '\0') *(ret + k - 1) = *(ret + (k++));
                            *(ret + k - 1) = '\0';
                            pos = ret - new_argv[i] + 1;
                            continue;
                        }
                        newsize = strlen(new_argv[i]) + strlen(arg_arr[7]) + 1;
                        tmp = calloc(newsize, sizeof(char));
                        new_argv[i] = realloc(new_argv[i], newsize * sizeof(char));
                        ret=strstr((new_argv[i] + pos), "$7");
                        ret[0] ='\0';
                        strcat(tmp,arg_arr[7]);
                        strcat(tmp, &(ret[2]));
                        strcat(new_argv[i], tmp);
                        free(tmp);
                    }
                }
            }
            if ((ret = strstr(new_argv[i], "$8")))
            {
                if(cnt_argc <= 8)
                {
                    fprintf(stderr,"Don't have such arg");
                }else
                {
                    pos = ret - new_argv[i];
                    while ((ret = strstr((new_argv[i] + pos), "$8")))
                    {
                        if ((ret != new_argv[i]) && (*(ret - 1) == '\\'))
                        {
                            k = 0;
                            while (*(ret + k) != '\0') *(ret + k - 1) = *(ret + (k++));
                            *(ret + k - 1) = '\0';
                            pos = ret - new_argv[i] + 1;
                            continue;
                        }
                        newsize = strlen(new_argv[i]) + strlen(arg_arr[8]) + 1;
                        tmp = calloc(newsize, sizeof(char));
                        new_argv[i] = realloc(new_argv[i], newsize * sizeof(char));
                        ret=strstr((new_argv[i] + pos), "$8");
                        ret[0] ='\0';
                        strcat(tmp,arg_arr[8]);
                        strcat(tmp, &(ret[2]));
                        strcat(new_argv[i], tmp);
                        free(tmp);
                    }
                }
            }
            if ((ret = strstr(new_argv[i], "$9")))
            {
                if(cnt_argc <= 9)
                {
                    fprintf(stderr,"Don't have such arg");
                }else
                {
                    pos = ret - new_argv[i];
                    while ((ret = strstr((new_argv[i] + pos), "$9")))
                    {
                        if ((ret != new_argv[i]) && (*(ret - 1) == '\\'))
                        {
                            k = 0;
                            while (*(ret + k) != '\0') *(ret + k - 1) = *(ret + (k++));
                            *(ret + k - 1) = '\0';
                            pos = ret - new_argv[i] + 1;
                            continue;
                        }
                        newsize = strlen(new_argv[i]) + strlen(arg_arr[9]) + 1;
                        tmp = calloc(newsize, sizeof(char));
                        new_argv[i] = realloc(new_argv[i], newsize * sizeof(char));
                        ret=strstr((new_argv[i] + pos), "$9");
                        ret[0] ='\0';
                        strcat(tmp,arg_arr[9]);
                        strcat(tmp, &(ret[2]));
                        strcat(new_argv[i], tmp);
                        free(tmp);
                    }
                }
            }
        }
        i++;
    }
    return new_argv;
}
void mhistory(void)
{
    int i;
    fprintf(stdout, "History \n");
    for(i = 0; i < his_count ; i++)
        fprintf(stdout, "%i %s \n" , i + 1, history[i]);
}
char* runHistory(char* commandLine)
{
    int num_comm = 0;

    if (commandLine[0] == '!')
    {
        if ((sscanf((commandLine + 1), "%d", &num_comm) > 0) && (num_comm <= his_count))
        {
            strcpy(commandLine,history[num_comm - 1]);
            commandLine[strlen(history[num_comm - 1])] = '\0';

            history[his_count] = calloc(1, strlen(commandLine) * sizeof(char) + 1);
            strcpy(history[his_count++], commandLine);
        }else
        {
            commandLine[0] = '\0';
            fprintf(stderr,"Error command \"!\"\n");
        }
    }
    else
    {
        history[his_count] = calloc(1, strlen(commandLine) * sizeof(char) + 1);
        strcpy(history[his_count++], commandLine);
    }
    return commandLine;
}

void m_cat(char* arg1)
{
    char* str;
    FILE *fp;
    int c;
    int size = CONSTSIZE, cursor = 0;

    size = CONSTSIZE;
    str = (char*)malloc(size*sizeof(char));

    if (arg1)
    {
        if ((fp = fopen(arg1,"r")) != NULL)
        {
            c = fgetc(fp);
            while(c != EOF)
            {
                cursor = 0;
                while((c != '\n') && (c != EOF))
                {
                    str[cursor++] = c;
                    c = fgetc(fp);
                    if(cursor == size)
                    {
                        size += CONSTSIZE;
                        str = (char*)realloc(str , size*sizeof(char));
                    }
                }
                str[cursor] = '\0';

                if (cursor > 0)
                {
                    cursor = 0;
                    fputs(str,stdout);
                    fputc('\n',stdout);
                }
                c = fgetc(fp);
            }
            fclose(fp);

        }else
        {
            fprintf(stderr, "File not found.\n");
        }

    }else
    {
        c = fgetc(stdin);

        while(c != EOF)
        {
            cursor = 0;
            while((c != '\n') && (c != EOF))
            {
                str[cursor++] = c;
                c = fgetc(stdin);
                if(cursor == size)
                {
                    size += CONSTSIZE;
                    str = (char*)realloc(str , size*sizeof(char));
                }
            }
            str[cursor] = '\0';

            if (cursor > 0)
            {
                cursor = 0;
                fputs(str,stdout);
                fputc('\n',stdout);
            }
            c = fgetc(stdin);
        }
    }
    free(str);
}

void m_grep(char* arg1, char* arg2)
{
    char* str;
    int c;
    int size = CONSTSIZE, cursor = 0;

    size = CONSTSIZE;
    str = (char*)malloc(size*sizeof(char));

    if (arg1)
    {
        c = fgetc(stdin);

        while(c != EOF)
        {
            cursor = 0;
            while((c != '\n') && (c != EOF))
            {
                str[cursor++] = c;
                c = fgetc(stdin);
                if(cursor == size)
                {
                    size += CONSTSIZE;
                    str = (char*)realloc(str , size*sizeof(char));
                }
            }
            str[cursor] = '\0';

            if (cursor > 0)
            {
                if ((arg2) && !strcmp(arg2,"-v"))
                {
                    if (!strstr(str,arg1))
                    {
                        fputs(str,stdout);
                        fputc('\n',stdout);
                    }
                }else
                {
                    if (strstr(str,arg1))
                    {
                        fputs(str,stdout);
                        fputc('\n',stdout);
                    }
                }
                cursor = 0;

            }
            c = fgetc(stdin);
        }
    }else
    {
        fprintf(stderr, "Command \"mgrep\" error.\n");
    }
    free(str);
}

int m_sed(char* arg1, char* arg2)
{
    char *str, *ret, *tmp;
    int c, pos, newsize;
    int size = CONSTSIZE, cursor = 0;

    size = CONSTSIZE;
    str = calloc(1, size * sizeof(char));
    c = fgetc(stdin);

    while(c != EOF)
    {
        cursor = 0;
        while((c != '\n') && (c != EOF))
        {
            str[cursor++] = c;
            c = fgetc(stdin);
            if(cursor == size)
            {
                size += CONSTSIZE;
                str = (char*)realloc(str , size*sizeof(char));
            }
        }
        str[cursor] = '\0';

        if (cursor > 0)
        {
            if ((ret = strstr(str, arg1)))
            {
                pos = ret - str;
                while ((ret=strstr((str + pos), arg1)))
                {
                    newsize = strlen(str) + strlen(arg2) + 1;
                    tmp = calloc(newsize, sizeof(char));
                    str = realloc(str, newsize * sizeof(char));
                    ret=strstr((str + pos), arg1);
                    ret[0] ='\0';
                    strcat(tmp,arg2);
                    strcat(tmp, &(ret[strlen(arg1)]));
                    strcat(str, tmp);
                    free(tmp);
                    pos = ret - str + strlen(arg2);
                }
            }
            fputs(str,stdout);
            fputc('\n',stdout);
            free(str);
            str = calloc(1, size * sizeof(char));
        }
        c = fgetc(stdin);
    }
    free(str);
    return 0;
}

void freeJob (struct job * cmd)
{
	int i;

	for (i = 0; i <cmd->numProgs; i++) {
		free(cmd->progs[i].argv);
		if (cmd->progs[i].redirections)
			free(cmd->progs[i].redirections);
		if (cmd->progs[i].freeGlob)
			globfree(&cmd->progs[i].globResult);
	}
	free(cmd->progs);
	if (cmd->text) free(cmd->text);
	free(cmd->cmdBuf);
}

void freeN(struct job *newJob)
{
    if (newJob->next)
    {
        freeN(newJob->next);
        free(newJob->next);
    }
    freeJob(newJob);
}
void m_exit(struct job *newJob, struct jobSet * jobList)
{
    int i;

    for(i = 0; i < his_count; i++) free(history[i]);
    free(history);

    freeJob(newJob);

    if (jobList->fg)
    {
        free(jobList->fg);
    }
    if (jobList->head)
    {
        freeN(jobList->head);
        free(jobList->head);
    }
    exit(0);
}

int getCommand(FILE * source, char * command) {
	if (source == stdin) {
        printf ("%s: ",USER);
		fflush(stdout);
	}

	if (!fgets(command, MAX_COMMAND_LEN, source)) {
		if (source == stdin) printf("\n");
		return 1;
	}

	/* удаление хвостового символа новой строки */
	command[strlen(command) - 1] = '\0';

	return 0;
}


/* Возвращаем cmd->numProgs как О, если не представлено ни одной команды
(например, пустая строка). Если будет обнаружена допустимая команда,
commandPtr будет ссылаться на начало следующей команды (если исходная
команда была связана с несколькими заданиями) или будет равно NULL,
если больше не представлено ни одной команды. */
int parseCommand (char ** commandPtr, struct job * job, int * isBg) {
	char * command;
	char * returnCommand = NULL;
	char * src, * buf, * chptr;
	int argc = 0;
	int done = 0;
	int argvAlloced;
	int i;
	char quote = '\0';
	int count;
	struct childProgram * prog;

	/* пропускаем первое свободное место (например, пробел) */
	while (**commandPtr && isspace(**commandPtr)) (*commandPtr)++;

	/* обрабатываем пустые строки и первые символы '#' */
	if (!**commandPtr || (**commandPtr == '#')) {
		job->numProgs = 0;
		*commandPtr = NULL;
		return 0;
	}

	*isBg = 0;
	job->numProgs = 1;
	job->progs = malloc(sizeof(*job->progs));

	/* Мы задаем элементы массива argv для ссылки внутри строки.
		Освобождение памяти осуществляется с помощью функции freeJob().

		Получив незанятую память, нам не нужно будет использовать завершающие
		значения NULL, поэтому оставшаяся часть будет выглядеть аккуратнее
		(хотя, честно говоря, менее эффективно). */
	job->cmdBuf = command = calloc(1, strlen(*commandPtr) + 3);
	job->text = NULL;

	prog = job->progs;
	prog->numRedirections = 0;
	prog->redirections = NULL;
	prog->freeGlob = 0;
	prog->isStopped = 0;

	argvAlloced = 5;
	prog->argv = malloc(sizeof(*prog->argv) * argvAlloced);
	prog->argv[0] = job->cmdBuf;

	buf = command;
	src = *commandPtr;
	while (*src && !done) {
		if (quote == *src)
		{
			quote = '\0';
			*buf++ = *src;
		} else if (quote)
		{
			/*if (*src == '\\')
			{
				src++;
				if (!*src) {
					fprintf(stderr,	"after \\ need symbol\n");

					freeJob(job);
					return 1;
				}

				if (*src != quote) *buf++ = '\\';
			}*/
			*buf++ = *src;
		} else if (isspace(*src))
		{
			if (*prog->argv[argc])
			{
				buf++, argc++;
				/* +1 здесь оставляет место для NULL, которое
					завершает массив argv */
				if ((argc + 1) == argvAlloced)
				{
					argvAlloced += 5;
					prog->argv = realloc(prog->argv,
						sizeof(*prog->argv) * argvAlloced);
                }
                prog->argv[argc] = buf;

                /*globLastArgument(prog, &argc, &argvAlloced);*/
            }
	} else switch (*src) {
		case '"':
		case '\'':
			quote = *src;
			*buf++ = *src;
			break;

		case '#': /* комментарий */
			done = 1;
			break;

		case '>': /* переадресации */
		case '<':
            if (!argc)
            {
				fprintf(stderr, "Not command\n");
				freeJob(job);
				commandPtr[0] = '\0';
				return 1;
			}
            if (*prog->argv[argc])
			{
				buf++, argc++;
				if ((argc + 1) == argvAlloced)
				{
					argvAlloced += 5;
					prog->argv = realloc(prog->argv, sizeof(*prog->argv) * argvAlloced);
                }
                prog->argv[argc] = buf;
            }
			i = prog->numRedirections++;
			prog->redirections = realloc(prog->redirections, sizeof(*prog->redirections) * (i + 1));

            if (*src == '>')
					prog->redirections[i].fd = 1;
				else
					prog->redirections[i].fd = 0;

 			if (*src++ == '>') {
				if (*src == '>') {
					prog->redirections[i].type = REDIRECT_APPEND;
					src++;
				} else {
					prog->redirections[i].type = REDIRECT_OVERWRITE;
				}
			}else
			{
                prog->redirections[i].type = REDIRECT_INPUT;
			}

			chptr = src;
			while (isspace(*chptr)) chptr++;

			if (!*chptr) {
				fprintf(stderr, "after %c file name\n", *src);

				freeJob(job);
				return 1;
			}

			prog->redirections[i].filename = buf;
			while (*chptr && !isspace(*chptr))
				*buf++ = *chptr++;

			src = chptr - 1; /* src++ будет сделано позже */
			prog->argv[argc] = ++buf;
			break;

		case '|' : /* канал */
			/* завершение этой команды */
			if (*prog->argv[argc]) argc++;
			if (!argc) {
				fprintf(stderr, "Empty command in channel\n");
				freeJob(job);
				commandPtr[0] = '\0';
				return 1;
			}
			prog->argv[argc] = NULL;

			/* и начало следующей */
			job->numProgs++;
			job->progs = realloc(job->progs,
								sizeof(*job->progs) *
								job->numProgs);
			prog = job->progs + (job->numProgs - 1);
			prog->numRedirections = 0;
			prog->redirections = NULL;
			prog->freeGlob = 0;
			prog->isStopped = 0;
			argc = 0;

			argvAlloced = 5;
			prog->argv = malloc(sizeof(*prog->argv) *
								argvAlloced);
			prog->argv[0] = ++buf;

			src++;
			while (*src && isspace(*src)) src++;

			if (!*src) {
				fprintf(stderr, "clear command in chanel\n");
				commandPtr[0] = '\0';
				freeJob(job);
				return 1;
			}
			src--; /* инкремент ++ мы сделаем в конце цикла */

			break;

		case '&' : /* фон */
			*isBg = 1;
		case ';': /* разнообразные команды */
			done = 1;
			returnCommand = *commandPtr + (src - *commandPtr) + 1;
			break;

		case '\\':
			src++;
			if (!*src) {
				freeJob(job);
				fprintf(stderr, "after \\ need symbol\n");
				return 1;
			}
			if (*src == '*' || *src == '[' || *src == ']' || *src == '?'|| *src == '$')
				*buf++ ='\\';
			/* неудача */
		default:
			*buf++ = *src;
		}

		src++;
	}

	if (*prog->argv[argc]) {
		argc++;
		/*globLastArgument(prog, &argc, &argvAlloced);*/
	}
	if (!argc) {
		freeJob(job);
		job->numProgs = 0;
		*commandPtr = returnCommand;
		return 0;
	}
	prog->argv[argc] = NULL;

	if (!returnCommand) {
		job->text = malloc(strlen(*commandPtr) +1);
		strcpy(job->text, *commandPtr);
	} else {
	/*Оставляем любые хвостовые пробелы, хотя и получится это несколько небрежно*/

		count = returnCommand - *commandPtr;
		job->text = malloc(count + 1);
		strncpy(job->text, *commandPtr, count);
		job->text[count] = '\0';
	}

	*commandPtr = returnCommand;

	return 0;
}

int setupRedirections(struct childProgram * prog) {
	int i;
	int openfd;
	int mode;
	struct redirectionSpecifier * redir = prog->redirections;

	for (i = 0; i < prog->numRedirections; i++, redir++) {
		switch (redir->type) {
		case REDIRECT_INPUT:
			mode = O_RDONLY;
			break;
		case REDIRECT_OVERWRITE:
			mode = O_RDWR | O_CREAT | O_TRUNC;
			break;
		case REDIRECT_APPEND:
			mode = O_RDWR | O_CREAT | O_APPEND;
			break;
		}

		openfd = open(redir->filename, mode, 0666);
		if (openfd<0) {
			/* мы могли потерять это в случае переадресации stderr,
			   хотя bash и ash тоже потеряют его (а вот
			   zsh — нет!) */
			fprintf(stderr, "ошибка при открытии %s: %s\n",
					redir->filename, strerror(errno));
			return 1;
		}

		if (openfd != redir->fd) {
			dup2(openfd, redir->fd);
			close(openfd);
		}
	}

	return 0;
}

int runCommand(struct job newJob, struct jobSet * jobList, int inBg )
{
	struct job * job;
	char * newdir, * buf;
	char** new_argv;
	int i, j, len;
	int nextin, nextout;
	int pipefds[2]; /* pipefdfO] предназначен для чтения */
	char * statusString;
	int jobNum;
	int controlfds[2] ;/*канал для возможности приостановки работы дочернего процесса*/

/* здесь производится обработка встраиваемых модулей — мы не используем fork(),
	поэтому, чтобы поместить процесс в фон, придется потрудиться */
	if (!strcmp(newJob.progs[0].argv[0], "exit")) {
		/* здесь возвращается реальный код выхода */
		m_exit(&newJob, jobList);
	}
/*	else if (!strcmp(newJob.progs[0].argv[0], "msed"))
	{
        m_sed(newJob.progs[0].argv[1],newJob.progs[0].argv[2]);
        return 0;
	}*/
    else if (!strcmp(newJob.progs[0].argv[0], "fg") || !strcmp(newJob.progs[0].argv[0], "bg"))
    {
		if (!newJob.progs[0].argv[1] || newJob.progs[0].argv[2]) {
			fprintf(stderr,	"%s: only 1 argument\n", newJob.progs[0].argv[0]);
			return 1;
		}
		if (sscanf(newJob.progs[0].argv[1], "%d", &jobNum) != 1) {
			fprintf (stderr, "%s: error argument '%s'\n",
						newJob.progs[0].argv[0],
						newJob.progs[0].argv[1]);
			return 1;
		}

		for (job = jobList->head; job; job = job->next)
			if (job->jobId == jobNum) break;

			if (!job) {
				fprintf(stderr, "%s: Unknow job %d\n",	newJob.progs[0].argv[0], jobNum);
			return 1;
		}

		if (*newJob.progs[0].argv[0] == 'f') {
			/* Переводим задание на передний план */

			if (tcsetpgrp(0, job->pgrp))
				perror("tcsetpgrp");
			jobList->fg = job;
		}

		/* Повторяем запуск процессов в задании */
		for (i = 0; i<job->numProgs; i++)
			job->progs[i].isStopped = 0;

		kill(-job->pgrp, SIGCONT);

		job->stoppedProgs = 0;

		return 0;
	}

	nextin = 0, nextout = 1;
	for (i = 0; i < newJob.numProgs; i++) {
		if ((i + 1) < newJob.numProgs) {
			pipe(pipefds);
			nextout = pipefds[1];
		} else {
			nextout = 1;
		}

		pipe(controlfds);
        new_argv = variables_Chg(newJob.progs[i].argv);

		if (!(newJob.progs[i].pid = fork()))
		{
			signal(SIGTTOU, SIG_DFL);
			signal(SIGTSTP, SIG_DFL);

			close(controlfds[1]);
	/* при чтении будет возвращен 0, когда записывающая сторона закрыта */
			read(controlfds [0], &len, 1);
			close(controlfds[0]);

			if (nextin != 0) {
				dup2(nextin, 0);
				close(nextin);
			}

			if (nextout != 1) {
				dup2(nextout, 1);
				close(nextout);
			}

			/* явные переадресации подменяют каналы */
			setupRedirections(newJob.progs + i) ;

            if (!strcmp(new_argv[0], "mcat"))
            {
                m_cat(new_argv[1]);
                j = 0;
                while (new_argv[j] != NULL) free(new_argv[j++]);
                free(new_argv);
                m_exit(&newJob, jobList);
            }
            else if (!strcmp(new_argv[0], "mgrep"))
            {
                if(new_argv[1] == NULL)
                    fprintf(stderr,"Error command mgrep\n");
                else
                    m_grep(new_argv[1],new_argv[2]);

                j = 0;
                while (new_argv[j] != NULL) free(new_argv[j++]);
                free(new_argv);
                m_exit(&newJob, jobList);
            }
            else if (!strcmp(new_argv[0], "msed"))
            {
                if((new_argv[1] == NULL) || (new_argv[2] == NULL))
                    fprintf(stderr,"Error command msed\n");
                else
                    m_sed(new_argv[1],new_argv[2]);

                j = 0;
                while (new_argv[j] != NULL) free(new_argv[j++]);
                free(new_argv);
                m_exit(&newJob, jobList);
            }
            else if (!strcmp(new_argv[0], "pwd"))
            {
                len = 50;
                buf = malloc(len);
                while (!getcwd(buf, len) && errno == ERANGE) {
                    len += 50;
                    buf = realloc(buf, len);
                }
                fputs(buf,stdout);
                fputc('\n',stdout);
                free(buf);

                j = 0;
                while (new_argv[j] != NULL) free(new_argv[j++]);
                free(new_argv);
                m_exit(&newJob, jobList);
            }
            else if (!strcmp(new_argv[0], "history"))
            {
                mhistory();
                j = 0;
                while (new_argv[j] != NULL) free(new_argv[j++]);
                free(new_argv);
                m_exit(&newJob, jobList);
            }
            else if (!strcmp(new_argv[0], "cd"))
            {
                if (!new_argv[1] == 1)
                    newdir = getenv("HOME");
                else
                    newdir = new_argv[1];
                if (chdir(newdir))
                    fprintf(stderr,"error(change current dir): %s\n", strerror(errno));

                j = 0;
                while (new_argv[j] != NULL) free(new_argv[j++]);
                free(new_argv);
                m_exit(&newJob, jobList);
            }
            else if (!strcmp(new_argv[0], "jobs"))
            {
                for (job = jobList->head; job; job = job->next)
                {
                    if (job->runningProgs == job->stoppedProgs)
                        statusString = "Stoped";
                    else
                        statusString = "Running";
                    buf = malloc(100);
                    sprintf(buf, JOB_STATUS_FORMAT, job->jobId, statusString, job->text);
                    fputs(buf,stdout);
                    fputc('\n',stdout);
                    free(buf);
                }
                j = 0;
                while (new_argv[j] != NULL) free(new_argv[j++]);
                free(new_argv);
                m_exit(&newJob, jobList);
            }
            else
            {
                execvp(new_argv[0], new_argv);
                fprintf(stderr, "Error ехес() for %s: %s\n", new_argv[0], strerror(errno));
                j = 0;
                while (new_argv[j] != NULL) free(new_argv[j++]);
                free(new_argv);
                m_exit(&newJob, jobList);
			}
		}
		j = 0;
		while (new_argv[j] != NULL) free(new_argv[j++]);
		free(new_argv);

		/* помещаем дочерний процесс в группу процессов, лидером в которой
			является первый процесс в этом канале */
		setpgid(newJob.progs[i].pid, newJob.progs[0].pid);

	/* закрываем канал управления, чтобы продолжить работу дочернего процесса */
		close(controlfds[0]);
		close(controlfds[1]);

		if (nextin != 0) close (nextin);
		if (nextout != 1) close(nextout);

		/* Если другого процесса нет, то nextin является "мусором",
			хотя это и не является помехой */
		nextin = pipefds[0];
	}

	newJob.pgrp = newJob.progs[0].pid;

	/* поиск идентификатора используемого задания */
	newJob.jobId = 1;
	for (job = jobList->head; job; job = job->next)
		if (job->jobId >= newJob.jobId)
			newJob.jobId = job->jobId + 1;

	/* добавляем задание в список выполняющихся заданий */
	if (!jobList->head) {
		job = jobList->head = malloc(sizeof(*job));
	} else {
		for (job = jobList->head; job->next; job = job->next);
		job->next = malloc(sizeof(*job));
		job = job->next;
	}

	*job = newJob;
	job->next = NULL;
	job->runningProgs = job->numProgs;
	job->stoppedProgs = 0;

	if (inBg) {
	/* мы не ожидаем возврата фоновых заданий - добавляем их
		в список фоновых заданий и оставляем их */

		printf("(%d) %d\n", job->jobId,
		newJob.progs[newJob.numProgs - 1].pid);
	} else {
		jobList->fg = job;

	/* перемещаем новую группу процессов на передний план */

		if (tcsetpgrp(0, newJob.pgrp))
			perror("tcsetpgrp");
	}

	return 0;
}

void removeJob(struct jobSet * jobList, struct job * job) {
	struct job * prevJob;

	freeJob(job);
	if (job == jobList->head) {
		jobList->head = job->next;
	} else {
		prevJob = jobList->head;
		while (prevJob->next != job) prevJob = prevJob->next;
		prevJob->next = job->next;
	}

	free(job);
}

/* Проверяем, завершился ли какой-либо фоновый процесс - если да, то
	устанавливаем причину и проверяем, окончилось ли выполнение задания */
void checkJobs(struct jobSet * jobList) {
	struct job * job;
	pid_t childpid;
	int status;
	int progNum;
	char * msg;

	while ((childpid = waitpid(-1, &status,
			WNOHANG | WUNTRACED)) > 0) {
		for (job = jobList->head; job; job = job->next) {
			progNum = 0;
		while(progNum < job->numProgs &&
				job->progs[progNum].pid != childpid)
			progNum++;
		if (progNum < job->numProgs) break;
	}

	if (WIFEXITED(status) || WIFSIGNALED(status)) {
		/* дочерний процесс завершил работу */
		job->runningProgs--;
		job->progs [progNum].pid = 0;

		if (!WIFSIGNALED(status))
			msg = "Завершено";
		else
			msg = strsignal(WTERMSIG(status)) ;

		if (!job->runningProgs) {
			printf(JOB_STATUS_FORMAT, job->jobId,
					msg, job->text);
			removeJob(jobList, job);
		}
	} else {
	/* выполнение дочернего процесса остановлено */
		job->stoppedProgs++;
		job->progs[progNum].isStopped = 1;

		if (job->stoppedProgs == job->numProgs) {
			printf (JOB_STATUS_FORMAT, job->jobId, "Stop",	job->text);
		}
	}
}

	if (childpid == -1 && errno != ECHILD)
	perror("waitpid");
}

void sig_handler(int sig)
{
	switch(sig)
	{
		case SIGINT:
			if (fgprogsPID)kill(fgprogsPID,SIGINT);
			break;
		case SIGTSTP:
            if (fgprogsPID)kill(fgprogsPID,SIGTSTP);
			break;
	}
}

int main(int argc, char ** argv) {
	char command[MAX_COMMAND_LEN + 1];
	char * nextCommand = NULL;
	struct jobSet jobList = { NULL, NULL };
	struct job newJob;
	FILE * input = stdin;
	int i;
	int status;
	int inBg;

    int his_size = 0;
    struct passwd *userInfo;

    UID = geteuid();
    userInfo = getpwuid(UID);
    USER = userInfo->pw_name;
    HOME = userInfo->pw_dir;
    SHELL[0] = 0;
    strcat(SHELL, getenv("PWD"));
    strcat(SHELL, "/");
    strcat(SHELL, &(argv[0][argv[0][0]=='.'?2:0]));
    chdir(HOME);
    PID = getpid();
    history = (char **)malloc(1000 * sizeof(char*));
    his_size = 1000;
    his_count = 0;

    stdinFD = dup(0);
    stdoutFD = dup(1);

    cnt_argc = argc;
    str_cntargc[0] = '0' + cnt_argc;
    str_cntargc[1] = '\0';
    arg_arr = argv;

	signal(SIGTTOU, SIG_IGN);

	signal(SIGINT , sig_handler);
	signal(SIGTSTP, sig_handler);

	while (1) {
		if (!jobList.fg) {
			/* на переднем плане нет ни одного задания */

			/* проверяем, не завершилось выполнение какого-либо фонового задания */
			checkJobs(&jobList);

			if (!nextCommand) {
				if (getCommand(input, command)) break;
				nextCommand = command;
				if (his_size < (his_count + 1))
				{
                    his_size += CONSTSIZE;
                    history = realloc(history,(his_size) * sizeof(char*));
                }
                nextCommand = runHistory(nextCommand);
			}

			if (!parseCommand(&nextCommand, &newJob, &inBg)
                    &&	newJob.numProgs) {
				runCommand(newJob, &jobList, inBg);
			}
		} else {
	/*задание выполняется на переднем плане; ожидаем, пока оно завершится */
			i = 0;
			while (!jobList.fg->progs[i].pid ||
                    jobList.fg->progs[i].isStopped) i++;

			fgprogsPID = jobList.fg->progs[i].pid;
			waitpid(jobList.fg->progs[i].pid, &status, WUNTRACED);
            status_fg = status;

			if (WIFSIGNALED(status) &&
				(WTERMSIG(status) !=SIGINT)) {
				printf("%s\n", strsignal(status));
			}

			if (WIFEXITED(status) || WIFSIGNALED(status)) {
			/* дочерний процесс завершил работу */
				jobList.fg->runningProgs--;
				jobList.fg->progs[i].pid = 0;

				if (!jobList.fg->runningProgs) {
				/* дочерний процесс завершил работу */

					removeJob(&jobList, jobList.fg);
					jobList.fg = NULL;

				/* переводим оболочку на передний план */
					if (tcsetpgrp(0, getpid()))
						perror("tcsetpgrp");
				}
			} else {
			/* выполнение дочернего процесса было остановлено */
				jobList.fg->stoppedProgs++;
				jobList.fg->progs[i].isStopped = 1;

				if (jobList.fg->stoppedProgs ==	jobList.fg->runningProgs)
				{
					printf ("\n" JOB_STATUS_FORMAT,	jobList.fg->jobId,	"Stoped", jobList.fg->text);
					jobList.fg = NULL;
				}
			}

			if (!jobList.fg) {
		/* переводим оболочку на передний план */
				if (tcsetpgrp(0, getpid()))
					perror("tcsetpgrp") ;
			}
		}
	}

    for(i = 0; i < his_count; i++) free(history[i]);
    free(history);

	return 0;
}
