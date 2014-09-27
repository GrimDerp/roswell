#include <stdio.h>
#include <stdlib.h>
#include <archive.h>
#include <archive_entry.h>

#include "opt.h"
#include "util.h"
#include "pull.h"
static int in_resume=0;
static char *flags =NULL;
struct install_impls *install_impl;

struct install_impls *impls_to_install[]={
  &impls_sbcl_bin,
  &impls_sbcl
};

extern int extract(const char *filename, int do_extract, int flags,const char* outputpath,Function2 f,void* p);

int installed_p(struct install_options* param)
{
  int ret;
  char* i;
  int pos;
  char *impl;
  pos=position_char("-",param->impl);
  if(pos!=-1) {
    impl=subseq(param->impl,0,pos);
  }else
    impl=q(param->impl);
  i=s_cat(homedir(),q("impls"),q(SLASH),q(impl),q("-"),q(param->version),q(SLASH),NULL);
  ret=directory_exist_p(i);
  s(i),s(impl);
  return ret;
}

int install_running_p(struct install_options* param)
{
  /* TBD */
  return 0;
}

int start(struct install_options* param)
{
  char* home= homedir();
  char* p;
  ensure_directories_exist(home);
  if(installed_p(param)) {
    printf("%s/%s are already installed.if you intend to reinstall by (TBD).\n",param->impl,param->version);
    return 0;
  }
  if(install_running_p(param)) {
    printf("It seems running installation process for $1/$2.\n");
    return 0;
  }
  p=cat(home,"tmp",SLASH,param->impl,"-",param->version,SLASH,NULL);
  ensure_directories_exist(p);
  s(p);

  p=cat(home,"tmp",SLASH,param->impl,"-",param->version,".lock",NULL);
  setup_signal_handler(p);
  touch(p);

  s(p);
  s(home);
  return 1;
}

char* download_archive_name(struct install_options* param)
{
  char* ret=cat(param->impl,"-",param->version,NULL);
  if(param->arch_in_archive_name==0) {
    ret=s_cat(ret,q("."),q((*(install_impl->extention))(param)),NULL);
  }else {
    ret=s_cat(ret,cat("-",param->arch,"-",param->os,".",q((*(install_impl->extention))(param)),NULL),NULL);
  }
  return ret;
}

int download(struct install_options* param)
{
  char* home= homedir();
  char* url=(*(install_impl->uri))(param);
  char* impl_archive;
  if(get_opt("skip.download")) {
    printf("Skip downloading %s\n",url);
  }else {
    printf("Downloading archive.:%s\n",url);
    /*TBD proxy support... etc*/
    if(url) {
      char* archive_name=download_archive_name(param);
      impl_archive=cat(home,"archives",SLASH,archive_name,NULL);
      s(archive_name);
      ensure_directories_exist(impl_archive);

      if(download_simple(url,impl_archive,0)) {
	printf("Failed to Download.\n");
	return 0;
	/* fail */
      }else{
	printf("download done:%s\n",url);
      }
    }
    s(impl_archive),s(url);
  }
  s(home);
  return 1;
}

LVal expand_callback(LVal v1,LVal v2) {
  char* path=(char*)v1;
  struct install_options* param=(struct install_options*)v2;
  if(param->arch_in_archive_name) {
    int pos=position_char("/",path);
    if(pos!=-1) {
      return (LVal)s_cat(cat(param->impl,"-",param->version,"-",param->arch,"-",param->os,NULL),subseq(path,pos,0),NULL);
    }
  }
  return (LVal)q(path);
}

int expand(struct install_options* param)
{
  char* home= homedir();
  char* argv[5]={"-xf",NULL,"-C",NULL,NULL};
  int argc=4;
  int pos;
  char* archive;
  char* dist_path;
  char *impl,*version;
  archive=download_archive_name(param);
  pos=position_char("-",param->impl);
  if(pos!=-1) {
    impl=subseq(param->impl,0,pos);
  }else
    impl=q(param->impl);
  version=q(param->version);
  if(!param->expand_path)
    param->expand_path=cat(home,"src",SLASH,impl,"-",version,SLASH,NULL);
  dist_path=param->expand_path;
  printf("Extracting archive. %s to %s\n",archive,dist_path);
  
  delete_directory(dist_path,1);
  ensure_directories_exist(dist_path);

  /* TBD log output */
  argv[1]=cat(home,"archives",SLASH,archive,NULL);
  argv[3]=cat(home,"src",SLASH,NULL);
  extract(argv[1], 1, ARCHIVE_EXTRACT_TIME,argv[3],expand_callback,param);

  s(argv[1]),s(argv[3]);
  s(impl);
  s(archive);
  s(home);
  s(version);
  return 1;
}

int configure(struct install_options* param)
{
  char *impl=param->impl,*version=param->version;
  char* home= homedir();
  char* confgcache= cat(home,"/src/",impl,"-",version,"/src/confg.cache",NULL);
  char* cd;
  char* configure;
  int ret;
  if(in_resume) {
    delete_file(confgcache);
  }
  if(flags==NULL) {
    flags=q("");
  }
  if(strcmp("gcl",impl)==0) {
    flags=s_cat(flags,q(" --enable-ansi"));
  }
  if(strcmp("ecl",impl)==0 ||strcmp("ecl",impl)==0 || strcmp("clisp",impl)==0) {
    flags=s_cat(flags,q(" --mandir="),q(home),q("/share/man"));
  }
  printf("Configuring %s/%s\n",impl,version);
  cd=cat(home,"src/",impl,"-",version,NULL);
  printf ("cd:%s\n",cd);
  change_directory(cd);
  /* pipe connect for logging cim_with_output_control */
  configure=cat("./configure ",flags," --prefix=",home,"/impls/",impl,"-",version,NULL);
  ret=system(configure);
  s(configure);
  s(cd);
  s(confgcache);
  s(home);
}

install_cmds install_full[]={
  start,
  download,
  expand,
  configure,
  NULL
};

int cmd_pull(int argc,char **argv)
{
  int ret=1,k;
  install_cmds *cmds=NULL;
  struct install_options param;
  param.os=uname();
  param.arch=uname_m();
  param.arch_in_archive_name=0;
  param.expand_path=NULL;
  if(argc!=1) {
    for(k=1;k<argc;++k) {
      char* version_arg=NULL;
      int i,pos;
      param.impl=argv[k];
      pos=position_char("/",param.impl);
      if(pos!=-1) {
	param.version=subseq(param.impl,pos+1,0);
	param.impl=subseq(param.impl,0,pos);
      }else {
        param.version=NULL;
	param.impl=q(param.impl);
      }

      for(install_impl=NULL,i=0;i<sizeof(impls_to_install)/sizeof(struct install_impls*);++i) {
	struct install_impls* j=impls_to_install[i];
	if(strcmp(param.impl,j->name)==0) {
	  install_impl=j;
	}
      }
      if(!install_impl) {
	printf("%s is not implemented for install.\n",param.impl);
	exit(EXIT_FAILURE);
      }
      for(cmds=install_impl->call;*cmds&&ret;++cmds)
	ret=(*cmds)(&param);
      if(ret) { // after install latest installed impl/version should be default for 'run'
        struct opts* opt=global_opt;
        struct opts** opts=&opt;
        int i;
        char* home=homedir();
        char* path=cat(home,"config",NULL);
        char* v=cat(param.impl,".version",NULL);
        char* version=param.version;
        for(i=0;version[i]!='\0';++i)
          if(version[i]=='-')
            version[i]='\0';
        set_opt(opts,"default.impl",param.impl,0);
        set_opt(opts,v,version,0);
        save_opts(path,opt);
        s(home),s(path),s(v);
      }
      s(param.version),s(param.impl),s(param.arch),s(param.os);
      s(param.expand_path);
    }
  }else {
    printf("what would you like to install?\n");
    exit(EXIT_FAILURE);
  }
  return ret;
}