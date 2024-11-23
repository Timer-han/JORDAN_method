#include <iostream>
#include <pthread.h>
#include <cfloat>

enum class io_status
{
	undef,
	error_open,
	error_read,
	success
};

class Args
{
public:
	int p = 0; // количество потоков
	int k = 0; // номер потока
	pthread_t tid = 1;
	const char * name = nullptr;
	double res = 0; // количество элементов, больших среднего
	double sum = 0;
	double average = 0;
	double count = 0;
	io_status error_type = io_status::undef;
	double error_flag = 0;
};

void synchronize(int p, double *a, int n);
io_status nums_counter(FILE * f, double* count);
void *thread_func(void *args);
int process_args(Args *a);

void synchronize(int p, double *a=nullptr, int n = 0) {
	static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
	static pthread_cond_t c_in = PTHREAD_COND_INITIALIZER;
	static pthread_cond_t c_out = PTHREAD_COND_INITIALIZER;
	static int t_in = 0;//количество вошедших потоков
	static int t_out = 0;//количество вышедших потоков
	static double *r = nullptr;
	int i;
	if (p <= 1) return;
	pthread_mutex_lock(&m);
	if (r == nullptr) r = a;
	else for(i = 0; i < n; i++) r[i] += a[i];
	t_in++;
	if (t_in >= p) {
		t_out = 0;
		pthread_cond_broadcast(&c_in);
	}
	else {
		while (t_in < p) {
			pthread_cond_wait(&c_in, &m);
		}
	}
	if (r != a){
		for (i = 0; i < n; i++) {
			a[i] = r[i];
		}
	}
	t_out++;
	if (t_out >= p) {
		t_in = 0;
		r = nullptr;
		pthread_cond_broadcast(&c_out);
	}
	else {
		while (t_out < p){
			pthread_cond_wait(&c_out, &m);
		}
	}
	pthread_mutex_unlock(&m);

}

io_status average_counter(FILE * f, double* count, double* sum)
{
    double a, b;
	int flag = 1;
    *count = 0;
    *sum = 0;
    
	if (fscanf(f, "%lf", &b) != 1 && !feof(f)) {
        return io_status::error_read;
    }
    
	for (;;) {
		a = b;
        if (fscanf(f, "%lf", &b) != 1) {
            break;
        }
		if (std::abs(a - b) <= DBL_EPSILON) {
			if (flag) {
				sum[0] += b;
				count[0]++;
			}
			sum[0] += b;
			count[0]++;
			flag = 0;
		} else {
			flag = 1;
		}
    }
    if (feof(f)) {
        return io_status::success;
    } else {
        return io_status::error_read;
    }
}

io_status res_counter(FILE * f, double* count, double average)
{
    double num;
    *count = 0;
    
    for (;;) {
        if (fscanf(f, "%lf", &num) != 1) {
            break;
        }
		if (num > average) {
	        count[0]++;
		}
    }
    if (feof(f)) {
        return io_status::success;
    } else {
        return io_status::error_read;
    }
}

void *thread_func(void *args)
{
	Args *a = (Args*) args;
	FILE * fp = fopen(a->name, "r");
	
	if (fp == nullptr) {
		a->error_type = io_status::error_open;
		a->error_flag = 1;
	}
	else {
		a->error_flag = 0;
	}
	synchronize(a->p, & a->error_flag, 1);
	if (a -> error_flag > 0) {
		if (fp != nullptr) fclose(fp);
		return nullptr;
	}

	a->error_type = average_counter(fp, &a->count, &a->sum);
	if (a->error_type != io_status::success) {
		a->error_flag = 1;
	} else {
		a->error_flag = 0;
	}
	synchronize(a->p, &a->error_flag, 1);
	if (a -> error_flag > 0) {
		if (fp != nullptr) fclose(fp);
		return nullptr;
	}

	synchronize(a->p, & a->sum, 1);
	synchronize(a->p, & a->count, 1);

	printf("sum of %s is %lf\n", a->name, a->sum);
	printf("count of %s is %lf\n", a->name, a->count);

	if (a->count > 0.5) {
		a->average = a->sum / a->count;
	} else {
		printf("Yep!\n");
		a->res = 0;
		fclose(fp);
		a -> error_type = io_status::success;
		synchronize(a->p, & a->res, 1);
		return nullptr;
	}
	synchronize(a->p);

	rewind(fp);
	a->error_type = res_counter(fp, &a->res, a->average);
	if (a->error_type != io_status::success) {
		a->error_flag = 1;
	} else {
		a->error_flag = 0;
	}
	synchronize(a->p, &a->error_flag, 1);
	if (a -> error_flag > 0) {
		if (fp != nullptr) fclose(fp);
		return nullptr;
	}

	fclose(fp);
	a -> error_type = io_status::success;
	synchronize(a->p, & a->res, 1);

	return nullptr;
}


int process_args(Args *a)
{
	for (int i = 0; i < a->p; i++) {
		if (a[i].error_type != io_status::success) {
			printf("Error in file %s\n", a[i].name);
			return 1;
		}
	}
	return 0;
}


int main(int argc, char *argv[])
{
	Args *a;
	if (argc == 1) {
		printf("[-] Usage: %s files!\n", argv[0]);
		return 1;
	}

	int p = argc - 1, error;
    a = new Args[p];
	if (a == nullptr) {
        printf("[-] Not enough memory!\n");
        return -1;
    }

	int k;
	for (k = 0; k < p; k++) {
		a[k].k = k;
		a[k].p = p;
		a[k].name = argv[k + 1];
	}

	for (k = 1; k < p; k++) {
		if ((error = pthread_create(&a[k].tid, nullptr, thread_func, a + k))) {
			return error;
		}
	}

	a[0].tid = pthread_self();
	thread_func(a + 0);

	for (k = 1; k < p; k++) {
		pthread_join(a[k].tid, nullptr);
	}
	// printf("Yep!\n");
	if (!process_args(a)) {
		printf("[+] Result = %lld\n", (long long) a[0].res);
	}
	delete[] a;
	return 0;
}