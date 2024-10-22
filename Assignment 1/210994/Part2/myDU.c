#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

void printError()
{
	printf("Unable to execute\n");
	exit(-1);
}

long folderSize(const char *path);

int main(int argc, char *argv[])
{
	if (argc != 2)
	{
		printError();
	}

	const char *path = argv[1];
	DIR *dirp = opendir(path);
	if (!dirp)
	{
		printError();
	}
	struct dirent *directory_entry;
	struct stat sbuf;
	stat(path, &sbuf);
	long size = sbuf.st_size;
	while ((directory_entry = readdir(dirp)) != NULL)
	{
		if (strcmp(directory_entry->d_name, ".") == 0 || strcmp(directory_entry->d_name, "..") == 0)
		{
			continue;
		}
		char pathname[500];
		strcpy(pathname, path);
		strcat(pathname, "/");
		strcat(pathname, directory_entry->d_name);

		struct stat fileStat;
		if (stat(pathname, &fileStat) == -1)
		{
			printError();
			continue;
		}
		if (S_ISREG(fileStat.st_mode))
		{
			size += fileStat.st_size;
		}
		else if (S_ISDIR(fileStat.st_mode))
		{
			int fd[2];
			pipe(fd);
			int pid = fork();
			if (pid < 0)
			{
				printError();
			}
			if (pid == 0)
			{
				close(fd[0]);
				long direcSize = folderSize(pathname);
				char buf[500];
				sprintf(buf, "%ld", direcSize);
				if (write(fd[1], buf, sizeof(buf)) != sizeof(buf))
				{
					printError();
					exit;
				}
				exit(EXIT_SUCCESS);
			}
			else
			{
				int status;
				wait(&status);
				close(fd[1]);
				char buf[500];
				if (read(fd[0], buf, sizeof(buf)) != sizeof(buf))
				{
					printError();
					exit;
				}
				long d = strtol(buf, 0, 10);
				size += d;
			}
		}
		else if (S_ISLNK(fileStat.st_mode))
		{
			char buf[500];
			ssize_t link_size;
			link_size = readlink(pathname, buf, sizeof(buf) - 1);
			buf[link_size] = '\0';
			strcpy(pathname, buf);
			struct stat linkStat;
			if (stat(pathname, &linkStat) == 0)
			{
				if (S_ISREG(linkStat.st_mode))
				{
					size += linkStat.st_size;
				}
				else if (S_ISDIR(linkStat.st_mode))
				{
					size += folderSize(buf);
				}
			}
			else
			{
				printError();
			}
		}
	}

	printf("%ld\n", size);

	return 0;
}

long folderSize(const char *path)
{
	struct stat sbuf;
	if (stat(path, &sbuf) == -1)
	{
		printError();
		return -1;
	}

	if (!S_ISDIR(sbuf.st_mode))
	{
		printError();
		return -1;
	}

	long size = sbuf.st_size;
	DIR *dirp = opendir(path);
	if (dirp == NULL)
	{
		perror("opendir");
		return -1;
	}

	struct dirent *directory_entry;
	while ((directory_entry = readdir(dirp)) != NULL)
	{
		if (strcmp(directory_entry->d_name, ".") == 0 || strcmp(directory_entry->d_name, "..") == 0)
		{
			continue;
		}

		char pathname[500];
		strcpy(pathname, path);
		strcat(pathname, "/");
		strcat(pathname, directory_entry->d_name);

		struct stat fileStat;
		if (stat(pathname, &fileStat) == -1)
		{
			printError();
			continue;
		}

		if (S_ISREG(fileStat.st_mode))
		{
			size += fileStat.st_size;
		}
		else if (S_ISDIR(fileStat.st_mode))
		{
			size += folderSize(pathname);
		}
		else if (S_ISLNK(fileStat.st_mode))
		{
			char buf[500];
			ssize_t link_size;
			link_size = readlink(pathname, buf, sizeof(buf) - 1);
			buf[link_size] = '\0';
			strcpy(pathname, buf);
			struct stat linkStat;
			if (stat(pathname, &linkStat) == 0)
			{
				if (S_ISREG(linkStat.st_mode))
				{
					size += linkStat.st_size;
				}
				else if (S_ISDIR(linkStat.st_mode))
				{
					size += folderSize(buf);
				}
			}
			else
			{
				printError();
			}
		}
	}

	closedir(dirp);
	return size;
}
