// Catherine Galkina, group 624, year 2017

#include <math.h>
#include <stdio.h>
#include <mpi.h>

#define N1_DEFAULT 1000
#define N2_DEFAULT 1000

// П = [0,2]x[0,2]
const double A1 = 0;
const double A2 = 2;
const double B1 = 0;
const double B2 = 2;

// Step q=3/2
const double q = 1.5;

// Eps = 10^-4
const double eps = 10e-4;

int N1, N2;

double F(double x, double y)
{
    double xx = x*x, yy = y*y;
    return 2*(xx + yy)*(1 - 2*xx*yy)*exp(1 - 2*xx*yy);
}

double phi(double x, double y)
{
    double xx = x*x, yy = y*y;
    return exp(1 - 2*xx*yy);
}

double f_node(double t)
{
    return (pow(1 + t, q) - 1)/(pow(2.0, q) - 1);
}

double x_i(int i)
{
    if (i < 0 || i > N1) return -1;
    double f = f_node((double)i/N1);
    return A2*f + A1*(1 - f);
}

double y_j(int j)
{
    if (j < 0 || j > N2) return -1;
    double f = f_node((double)j/N2);
    return B2*f + B1*(1 - f);
}

void distribute_points(int n, int rank, int size, int &start, int &end)
{
    // n1*procs1 + n2*procs2 = n
    int procs1 = n % size;
    int n1 = n/size + 1;
    int procs2 = n - procs1;
    int n2 = n/size;
    if (rank < procs1) {
        start = rank*n1;
        end = start + n1 - 1;
    } else {
        start = procs1*n1 + (rank - procs1)*n2;
        end = start + n2 - 1;
    }
}

class Node
{
    // Rank
    int rank;
    // Column
    int my_i;
    // Row
    int my_j;

    // Ranks of neighbours
    int left;
    int right;
    int up;
    int down;

    // Points distribution
    int x1;
    int x2;
    int nx;
    int y1;
    int y2;
    int ny;
    double *xs;
    double *ys;
    double **p;
    double **p_prev;
    double **r;
    double **g;
    double **l;

    int step;

    bool border_point(int i, int j)
    {
        return ((i >= 0) && (j >= 0) && (i <= N1) && (j <= N2) &&
                (!i || !j || (i == N1) || (j == N2)));
    }

    double h1(int i)
    {
        return (xs[i+1-x1+1] - xs[i-x1+1]);
    }
    double h2(int j) { return (ys[j+1-y1+1] - ys[j-y1+1]); }
    double h1_(int i) { return 0.5*(h1(i) + h1(i-1)); }
    double h2_(int j) { return 0.5*(h2(j) + h2(j-1)); }
    double dot(int from_i, int to_i, int from_j, int to_j,
               double **u, double **v)
    {
        double sum = 0;
        for (int i = from_i; i <= to_i; i++)
            for (int j = from_j; j <= to_j; j++)
                sum += h1_(i)*h2_(j)*u[i-x1+1][j-y1+1]*v[i-x1+1][j-y1+1];
        return sum;
    }
    double laplace(int i, int j, double **f)
    {
        if (i < 1 || j < 1 || i > N1-1 || j > N2-1 ||
            i < x1 || i > x2 || j < y1 || j > y2) {
            return 0;
        }
        int ii = i - x1 + 1;
        int jj = j - y1 + 1;
        double fij = f[ii][jj];
        double fim1j = f[ii-1][jj];
        double fip1j = f[ii+1][jj];
        double fijm1 = f[ii][jj-1];
        double fijp1 = f[ii][jj+1];
        double f1 = (fij - fim1j)/h1(i-1) - (fip1j - fij)/h1(i);
        double f2 = (fij - fijm1)/h2(j-1) - (fijp1 - fij)/h2(j);
        return f1/h1_(i) + f2/h2_(j);

    }

public:
    Node(int rn, int rows, int cols)
    {
        rank = rn;
        int j = rn/cols;
        int i = rn - j*cols;
        my_i = i;
        my_j = j;

        int left_i = (i-1 >= 0) ? i-1 : cols - 1;
        int right_i = (i+1)%cols;
        int up_j = (j-1 >= 0) ? j-1 : rows - 1;
        int down_j = (j+1)%rows;
        left = (rank%cols) ? rank - 1 : -1;
        right = ((rank+1)%cols) ? rank + 1 : -1;
        up = (rank-cols >= 0) ? rank - cols : -1;
        down = (rank+cols < rows*cols) ? rank + cols : -1;

        distribute_points(N1+1, i, cols, x1, x2);
        distribute_points(N2+1, j, rows, y1, y2);
        nx = x2 - x1 + 1;
        ny = y2 - y1 + 1;

        step = 0;
        xs = NULL;
        ys = NULL;
        p = NULL;
        p_prev = NULL;
        r = NULL;
        g = NULL;
        l = NULL;
    }

    void Init()
    {
        int i, j;
        // + 2 is for neighbours
        xs = new double [nx+2];
        ys = new double [ny+2];
        for (i = x1-1; i <= x2+1; i++)
            xs[i-x1+1] = x_i(i);
        for (j = y1-1; j <= y2+1; j++)
            ys[j-y1+1] = y_j(j);

        p_prev = new double*[nx+2];
        for (i = x1-1; i <= x2+1; i++) {
            p_prev[i-x1+1] = new double[ny+2];
            for (j = y1-1; j <= y2+1; j++) {
                if (border_point(i, j))
                    p_prev[i-x1+1][j-y1+1] = phi(xs[i-x1+1], ys[j-y1+1]);
                else
                    p_prev[i-x1+1][j-y1+1] = 0;
            }
        }

        p = new double*[nx+2];
        for (i = x1-1; i <= x2+1; i++) {
            p[i-x1+1] = new double[ny+2];
            for (j = y1-1; j <= y2+1; j++) {
                p[i-x1+1][j-y1+1] = 0;
            }
        }

        r = new double*[nx+2];
        for (i = x1-1; i <= x2+1; i++) {
            r[i-x1+1] = new double[ny+2];
            for (j = y1-1; j <= y2+1; j++) {
                r[i-x1+1][j-y1+1] = 0;
            }
        }

        int start_i, end_i, start_j, end_j;
        start_i = x1 > 1 ? x1-1 : 1;
        end_i = x2 < N1-1 ? x2+1 : N1-1;
        start_j = y1 > 1 ? y1-1 : 1;
        end_j = y2 < N2-1 ? y2+1 : N2-1;
        for (i = start_i; i <= end_i; i++)
            for (j = start_j; j <= end_j; j++)
                r[i-x1+1][j-y1+1] =
                    -laplace(i, j, p_prev) - F(xs[i-x1+1], ys[j-y1+1]);

        g = new double*[nx+2];
        for (i = x1-1; i <= x2+1; i++) {
            g[i-x1+1] = new double[ny+2];
            for (j = y1-1; j <= y2+1; j++) {
                g[i-x1+1][j-y1+1] = r[i-x1+1][j-y1+1];
            }
        }

        l = new double*[nx+2];
        for (i = x1-1; i <= x2+1; i++) {
            l[i-x1+1] = new double[ny+2];
            for (j = y1-1; j <= y2+1; j++) {
                l[i-x1+1][j-y1+1] = 0;
            }
        }
    }

    double Step()
    {
        step++;
        int i, j;
        if (step == 1) {
            int start_i, end_i, start_j, end_j;
            start_i = x1 > 1 ? x1-1 : 1;
            end_i = x2 < N1-1 ? x2+1 : N1-1;
            start_j = y1 > 1 ? y1-1 : 1;
            end_j = y2 < N2-1 ? y2+1 : N2-1;
            for (i = start_i; i <= end_i; i++)
                for (j = start_j; j <= end_j; j++)
                    l[i-x1+1][j-y1+1] = -laplace(i, j, r);
            double tau =
                dot(start_i, end_i, start_j, end_j, r, r)/dot(start_i, end_i, start_j, end_j, l, r);
            for (i = x1; i <= x2; i++)
                for (j = y1; j <= y2; j++) {
                    int ii = i-x1+1, jj = j-y1+1;
                    p[ii][jj] = p_prev[ii][jj] - tau*r[ii][jj];
                }
        } else {
            int start_i, end_i, start_j, end_j;
            start_i = x1 > 1 ? x1-1 : 1;
            end_i = x2 < N1-1 ? x2+1 : N1-1;
            start_j = y1 > 1 ? y1-1 : 1;
            end_j = y2 < N2-1 ? y2+1 : N2-1;
            for (i = start_i; i <= end_i; i++)
                for (j = start_j; j <= end_j; j++)
                    l[i-x1+1][j-y1+1] = -laplace(i, j, r);
            double tmp1 =
                dot(start_i, end_i, start_j, end_j, l, g);
            for (i = start_i; i <= end_i; i++)
                for (j = start_j; j <= end_j; j++)
                    l[i-x1+1][j-y1+1] = -laplace(i, j, g);
            double tmp2 =
                dot(start_i, end_i, start_j, end_j, l, g);
            double alpha = tmp1/tmp2;
            for (i = x1; i <= x2; i++)
                for (j = y1; j <= y2; j++)
                    g[i-x1+1][j-y1+1] =
                        r[i-x1+1][j-y1+1] - alpha*g[i-x1+1][j-y1+1];
            for (i = start_i; i <= end_i; i++)
                for (j = start_j; j <= end_j; j++) {
                    r[i-x1+1][j-y1+1] =
                        -laplace(i, j, p_prev) - F(xs[i-x1+1], ys[j-y1+1]);
                    l[i-x1+1][j-y1+1] = -laplace(i, j, g);
                }
            double tau =
                dot(start_i, end_i, start_j, end_j, r, g)/dot(start_i, end_i, start_j, end_j, l, g);
            for (i = x1; i <= x2; i++)
                for (j = y1; j <= y2; j++) {
                    int ii = i-x1+1, jj = j-y1+1;
                    p[ii][jj] = p_prev[ii][jj] - tau*g[ii][jj];
                }
        }
        for (i = x1; i <= x2; i++)
            for (j = y1; j <= y2; j++) {
                int ii = i-x1+1, jj = j-y1+1;
                p_prev[ii][jj] = p[ii][jj] - p_prev[ii][jj];
            }
        double my_err = dot(x1, x2, y1, y2, p_prev, p_prev);
        double err;
        MPI_Allreduce(&my_err, &err, 1, MPI_FLOAT, MPI_SUM, MPI_COMM_WORLD);
        for (i = x1; i <= x2; i++)
            for (j = y1; j <= y2; j++) {
                int ii = i-x1+1, jj = j-y1+1;
                p_prev[ii][jj] = p[ii][jj];
            }
        return sqrt(err);
    }

    void Print()
    {
        const char* format
            = "%d i %d\n"
              "%d j %d\n"
              "%d left %d\n"
              "%d right %d\n"
              "%d up %d\n"
              "%d down %d\n"
              "%d x1 %d x2 %d\n"
              "%d y1 %d y2 %d\n";
        printf(format, rank, my_i, rank, my_j, rank, left,
                rank, right, rank, up, rank, down,
                rank, x1, x2,
                rank, y1, y2);
        for (int i = x1 - 1; i <= x2 + 1; i++)
            for (int j = y1 - 1; j <= y2 + 1; j++)
                printf("%d %d %d %d %f\n", rank, i, j,
                        border_point(i, j), p_prev[i-x1+1][j-y1+1]);
    }

    ~Node() {
        if (xs)
            delete [] xs;
        if (ys)
            delete [] ys;
        int i;
        if (p) {
            for (i = 0; i < nx+2; i++)
                delete [] p[i];
            delete [] p;
        }
        if (p_prev) {
            for (i = 0; i < nx+2; i++)
                delete [] p_prev[i];
            delete [] p_prev;
        }
        if (r) {
            for (i = 0; i < nx+2; i++)
                delete [] r[i];
            delete [] r;
        }
        if (g) {
            for (i = 0; i < nx+2; i++)
                delete [] g[i];
            delete [] g;
        }
        if (l) {
            for (i = 0; i < nx+2; i++)
                delete [] l[i];
            delete [] l;
        }
    }
};

void distribute_procs(int procs, int &rows, int &cols)
{
    int l = trunc(log2(procs));
    int rows_pow = l/2 + l%2;
    int cols_pow = l/2;
    rows = rows_pow ? 2<<(rows_pow-1) : 1; // 2^rows_pow
    cols = cols_pow ? 2<<(cols_pow-1) : 1; // 2^cols_pow
}

int main(int argc, char **argv)
{
    // Parse args
    if (argc >= 3) {
        int check = sscanf(argv[1], "%d", &N1);
        N1 = check ? N1 : N1_DEFAULT;
        check = sscanf(argv[2], "%d", &N2);
        N2 = check ? N2 : N2_DEFAULT;
    } else {
        N1 = N1_DEFAULT;
        N2 = N2_DEFAULT;
    }

    MPI_Init(&argc, &argv);
    int rank, procs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &procs);

    int rows, cols;
    distribute_procs(procs, rows, cols);
    if (!rank)
        printf("ROWS %d COLS %d\n", rows, cols);

    Node me(rank, rows, cols);
    me.Init();

    double err = 0;
    do {
        err = me.Step();
        //printf("%d %f\n", rank, err);
    } while (err >= eps);

    MPI_Finalize();
    return 0;
}
