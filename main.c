#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <json-c/json.h>
#include <zconf.h>
#include <sys/wait.h>

#define SCALING_FACTOR 0.1
#define similarity_th 0.80

static char* escapeCSV(char* in) {
    int in_len = strlen(in);
    char *out_buf = malloc(in_len*2+3);
    int out_idx = 0;
    int in_idx = 0;

    for(in_idx=0; in_idx < in_len; in_idx++) {
        if(in[in_idx] == '"') {
            out_buf[out_idx++] = '\"';
            out_buf[out_idx++] = '\"';
        } else {
            out_buf[out_idx++] = in[in_idx];
        }
    }
    out_buf[out_idx++] = 0;
    return out_buf;
}


static int max(int x, int y) {
    return x > y ? x : y;
}

static int min(int x, int y) {
    return x < y ? x : y;
}

static double jaro_winkler_distance(const char *s, const char *a) {
    int i, j, l;
    int m = 0, t = 0;
    int sl = strlen(s);
    int al = strlen(a);
    int sflags[sl], aflags[al];
    int range = max(0, max(sl, al) / 2 - 1);
    double dw;

    if (!sl || !al)
        return 0.0;

    for (i = 0; i < al; i++)
        aflags[i] = 0;

    for (i = 0; i < sl; i++)
        sflags[i] = 0;

    /* calculate matching characters */
    for (i = 0; i < al; i++) {
        for (j = max(i - range, 0), l = min(i + range + 1, sl); j < l; j++) {
            if (a[i] == s[j] && !sflags[j]) {
                sflags[j] = 1;
                aflags[i] = 1;
                m++;
                break;
            }
        }
    }

    if (!m)
        return 0.0;

    /* calculate character transpositions */
    l = 0;
    for (i = 0; i < al; i++) {
        if (aflags[i] == 1) {
            for (j = l; j < sl; j++) {
                if (sflags[j] == 1) {
                    l = j + 1;
                    break;
                }
            }
            if (a[i] != s[j])
                t++;
        }
    }
    t /= 2;

    /* Jaro distance */
    dw = (((double)m / sl) + ((double)m / al) + ((double)(m - t) / m)) / 3.0;

    /* calculate common string prefix up to 4 chars */
    l = 0;
    for (i = 0; i < min(min(sl, al), 4); i++)
        if (s[i] == a[i])
            l++;

    /* Jaro-Winkler distance */
    dw = dw + (l * SCALING_FACTOR * (1 - dw));

    //fprintf_ln(stderr, _("jaroW: %lf\n"),dw);
    return dw;
}

static void executeRegexJar(const char *group_id)
{
    //if (groupId_list.used) {
    if (group_id) {

        //int length = 5 + groupId_list.used; //groupId_list.nr know only at runtime
        int length = 5 + 1; //groupId_list.nr know only at runtime
        const char **id_array = malloc(sizeof(*id_array) * length);

        id_array[0] = "/usr/bin/java";
        id_array[1] = "-jar";
        id_array[2] = "/Users/manan/CLionProjects/git/search-and-replace/RandomSearchReplaceTurtle.jar";
        id_array[3] = "/Users/manan/CLionProjects/git/search-and-replace/"; //config.properties path

        //for (int j = 0; j < groupId_list.used; j++) {
        //id_array[j+4] = &groupId_list.array[j];
        //}
        id_array[4] = group_id;
        id_array[length - 1] = NULL; //terminator need for execv

        //TODO adjust the path to jar file
        pid_t pid = fork();
        if (pid == 0) { // child process
            /* open /dev/null for writing */
            //printf("\n\n\nJAR PROCESS IS STARTING IN BACKGROUND!!!!!\n\n\n");
            int fd = open("/dev/null", O_WRONLY);
            dup2(fd, 1);    /* make stdout a copy of fd (> /dev/null) */
            close(fd);
            execv("/usr/bin/java",(void*)id_array);
        } else { //parent process
            int status;
            (void)waitpid(pid, &status, 0);
            if(status == -1){
                printf("RandomSearchAndRepalce END With Error\n");
            }
        }
        free(id_array);
    }
}


static const char* get_conflict_json_id(char* conflict,char* resolution)
{
    struct json_object *file_json = json_object_from_file(".git/rr-cache/conflict_index.json");
    if (!file_json) { // if file is empty
        if (!resolution) { //resolution is NULL
            return  NULL;
        }
        return "1";
    }

    double jaroW_conf = 0 ;
    double jaroW_resol = 0 ;
    //size_t leve = 0 ;
    const char* groupId = NULL;
    double max_sim_conf = similarity_th;
    double max_sim_resol = similarity_th;
    int idCount = 0;

    struct json_object *obj;
    const char* jconf;
    const char* jresol;
    int arraylen;

    double total_similarity_conf = 0;
    double total_similarity_resol = 0;
    json_object_object_foreach(file_json,key,val){
        obj = NULL;
        arraylen = json_object_array_length(val);
        idCount += 1;
        total_similarity_conf = 0;
        total_similarity_resol = 0;
        for (int i = 0; i < arraylen; i++) {
            obj = json_object_array_get_idx(val, i);
            jconf = json_object_get_string(json_object_object_get(obj, "conflict"));
            jresol = json_object_get_string(json_object_object_get(obj, "resolution"));

            if (resolution) {
                if (strcmp(conflict, jconf) == 0 && strcmp(resolution, jresol) == 0) {
                    json_object_put(file_json);
                    return NULL;
                }
            }
            jaroW_conf = jaro_winkler_distance(conflict,jconf);
            jaroW_resol = jaro_winkler_distance(resolution,jresol);
            //total_similarity += jaroW;
            total_similarity_conf += jaroW_conf;
            total_similarity_resol += jaroW_resol;
        }
        double avg_conf = total_similarity_conf/arraylen;
        double avg_resol = total_similarity_resol/arraylen;
        if (avg_conf >= max_sim_conf && avg_resol >= max_sim_resol) {
            max_sim_conf = avg_conf;
            max_sim_resol = avg_resol;
            groupId = key;
        }
    }
    char * id = malloc(sizeof(char*));
    //json_object_put(file_json);
    if (!groupId && resolution) { //if group == null and resolution != null
        //create new group id
        struct json_object *id_int = json_object_new_int(idCount+1);
        id = (char*) json_object_to_json_string(id_int);
    } else if (groupId) { //groupid != null
        memcpy(id,groupId, strlen(groupId)+1);
    }
    json_object_put(file_json);
    return id;
}

static int write_json_object(char* file_name, const char* group_id, char* conflict, char* resolution){
    printf("Login: write_json_object\n");

    struct json_object *file_json = json_object_from_file(".git/rr-cache/conflict_index.json");

    if (!file_json) // if file is empty
        file_json = json_object_new_object();

    struct json_object *object = json_object_new_object();
    struct json_object *jarray = json_object_new_array();

    json_object_object_get_ex(file_json, group_id,&jarray);

    if(!jarray)
        jarray = json_object_new_array();

    if (json_object_array_length(jarray)) { // get object id if exists
        //add new line to object id1
        json_object_object_add(object, "conflict", json_object_new_string(conflict));
        json_object_object_add(object, "resolution", json_object_new_string(resolution));
        json_object_array_add(jarray,object);
    } else { // if id not exists
        json_object_object_add(object, "conflict", json_object_new_string(conflict));
        json_object_object_add(object, "resolution", json_object_new_string(resolution));
        json_object_array_add(jarray,object);
        //json_object_object_add(file_json,group_id,jarray);
        json_object_object_add(file_json,group_id,json_object_get(jarray));
    }

    FILE *fp = fopen(file_name,"w");
    if (!fp){
        printf("Exit: write_json_object: not open FILE conflict_index\n");
        return 0;
    }
    //update or add groupid to file
    fprintf(fp,"%s", json_object_to_json_string_ext(file_json,2));
    fclose(fp);
    json_object_put(file_json);
    printf("Exit: write_json_object\n");
    return 1;
}

static int write_json_conflict_index(char* conflict, char* resolution)
{
    printf("Login: write_json_conflict_index\n");


    const char* group_id = get_conflict_json_id(conflict,resolution);

    if (!group_id){
        printf("Exit: write_json_conflict_index NO groupID\n");
        return 0;
    }

    if(!write_json_object(".git/rr-cache/conflict_index.json",group_id,conflict,resolution)){ //if return 0
        printf("Exit: write_json_conflict_index: write json object error\n");
        return 0;
    }

    //this will create a file with all the conflicts that are present in the clusters
    /*
    struct json_object *conflict_list = json_object_from_file(".git/rr-cache/conflict_list.json");

    if (!conflict_list) // if file is empty
        conflict_list = json_object_new_object();

    if(!write_json_object(conflict_list,".git/rr-cache/conflict_list.json","conflicts_list",conflict,resolution)){ //if return 0
        return 0;
    }
    */

    executeRegexJar(group_id);

    printf("Exit: write_json_conflict_index\n");
    return 1;
}

static void regex_repalce_suggestion(char *conflict, char *resolution)
{
    printf("Enter: regex_repalce_suggestion\n");
    const char *groupId = get_conflict_json_id(conflict,NULL);

    if (!groupId){
        printf("Exit: regex_repalce_suggestion: No GroupId\n");
        return;
    }

    pid_t pid = fork();
    if (pid == 0) { // child process
        /* open /dev/null for writing */
        int fd = open(".git/rr-cache/string_replace.txt", O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
        dup2(fd, 1);    /* make stdout a copy of fd (> /dev/null) */
        //dup2(fd, 2);    /* ...and same with stderr */
        close(fd);
        execl("/usr/bin/java", "/usr/bin/java", "-jar", "/Users/manan/CLionProjects/git/search-and-replace/RegexReplacement.jar","/Users/manan/CLionProjects/git/search-and-replace/",groupId,conflict,(char*)0);
    } else { //parent process
        int status;
        (void)waitpid(pid, &status, 0);
        //printf("\nJar Process terminate with code: %d\n",status);

        if (!status) {
            FILE *fp = fopen(".git/rr-cache/string_replace.txt", "r");
            if(!fp)
                return;

            fseek(fp, 0, SEEK_END); // goto end of file
            if (ftell(fp) == 0)
            {
                printf("file is empty\n");
                unlink(".git/rr-cache/string_replace.txt");
                printf("Exit: regex_repalce_suggestion\n");
                return;
            }
            fseek(fp, 0, SEEK_SET);

            char buffer1[500],buffer2[500],buffer3[500];
            char *regex1=NULL, *regex2=NULL, *replace1=NULL, *replace2=NULL, *res1=NULL, *res2=NULL;
            // read first resolution

            if (fgets(buffer1, 500, fp) && fgets(buffer2, 500, fp) && fgets(buffer3, 500, fp)) {
                regex1 = buffer1;
                replace1 = buffer2;
                res1 = buffer3;
                //fprintf_ln(stderr, _("Regex: %s"), buf.buf);
            }

            // read second resolution
            if (fgets(buffer1, 500, fp) && fgets(buffer2, 500, fp) && fgets(buffer3, 500, fp)) {
                regex2 = buffer1;
                replace2 = buffer2;
                res2 = buffer3;
                //fprintf_ln(stderr, _("Regex: %s"), buf.buf);
            }
            fclose(fp);

            fp = fopen(".git/rr-cache/regex_replace_result.txt","a+");
            conflict = escapeCSV(conflict);

            if(res1 && res2){
                double jw1 = jaro_winkler_distance(resolution,res1);
                double jw2 = jaro_winkler_distance(resolution,res2);
                if(jw1 >= jw2){
                    regex1[strcspn(regex1,"\n")] = 0;
                    replace1[strcspn(replace1,"\n")] = 0;
                    res1[strcspn(res1,"\n")] = 0;
                    regex1 = escapeCSV(regex1);
                    replace1 = escapeCSV(replace1);
                    res1 = escapeCSV(res1);
                    fprintf(fp,"\"%s\",\"%s\",\"%f\",\"%s\",\"%s\",\"%s\"\n",conflict,groupId,jw1,regex1,replace1,res1);
                    free(conflict);
                    free(regex1);
                    free(replace1);
                    free(res1);
                }else{
                    regex2 = escapeCSV(regex2);
                    replace2 = escapeCSV(replace2);
                    res2 = escapeCSV(res2);
                    regex2[strcspn(regex2,"\n")] = 0;
                    replace2[strcspn(replace2,"\n")] = 0;
                    res2[strcspn(res2,"\n")] = 0;
                    fprintf(fp,"\"%s\",\"%s\",\"%f\",\"%s\",\"%s\",\"%s\"\n",conflict,groupId,jw2,regex2,replace2,res2);
                    free(conflict);
                    free(regex2);
                    free(replace2);
                    free(res2);
                }
            }else{
                double jw1 = jaro_winkler_distance(resolution,res1);
                regex1 = escapeCSV(regex1);
                replace1 = escapeCSV(replace1);
                res1 = escapeCSV(res1);
                regex1[strcspn(regex1,"\n")] = 0;
                replace1[strcspn(replace1,"\n")] = 0;
                res1[strcspn(res1,"\n")] = 0;
                fprintf(fp,"\"%s\",\"%s\",\"%f\",\"%s\",\"%s\",\"%s\"\n",conflict,groupId,jw1,regex1,replace1,res1);
                free(conflict);
                free(regex1);
                free(replace1);
                free(res1);
            }
            fclose(fp);
            unlink(".git/rr-cache/string_replace.txt");
        }else{
            printf("Exit: regex repalce jar end with error\n");
        }
    }
    printf("Exit: regex_repalce_suggestion\n");
}

int main() {

    //ADJUST FILES PATH !!!!!

    struct json_object *file_json = json_object_from_file("/Users/manan/Tesi/results/ant/cluster-13.json");
    if (!file_json) // if file is empty
        return 0;

    struct json_object *obj;
    char* jconf=NULL;
    char* jresol=NULL;
    int arraylen;

    json_object_object_foreach(file_json,key,val){
        obj = NULL;
        arraylen = json_object_array_length(val);
        //printf("arraylen: %d\n",arraylen);
        for (int i = 0; i < arraylen; i++) {
            printf("i = %d\n",i+1);
            obj = json_object_array_get_idx(val, i);
            jconf = (char*) json_object_get_string(json_object_object_get(obj, "conflict"));
            jresol = (char*) json_object_get_string(json_object_object_get(obj, "resolution"));

            printf("jconf: %s\n",jconf);
            printf("jresol: %s\n",jresol);

            //synthesize a resolution for conflict
            regex_repalce_suggestion(jconf,jresol);
            //inset conflict and resoltuion in cluster
            write_json_conflict_index(jconf,jresol);
        }
    }
    json_object_put(file_json);
    return 0;
}
