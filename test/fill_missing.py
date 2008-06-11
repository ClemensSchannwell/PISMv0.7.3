#!/usr/bin/env python
from numpy import *
# This script is an implementation of the SOR method with Chebyshev
# acceleration for the Laplace equation, as described in 'Numerical Recipes in
# Fortran: the art of scientific computing' by William H. Press et al -- 2nd
# edition.

# This can (obviously) be optimized, but I believe that we _should not_
# sacrifice readability unless we absolutely have to.

# Note also that this script can be used both from the command line and as a
# Python module -- by adding 'from fill_missing import laplace' to your
# program.

# CK, 06/10/2008

def rho_jacobi((J,L)):
    """Computes $\rho_{Jacobi}$, see formula (19.5.24), page 858."""
    return (cos(pi/J) + cos(pi/L))/2

def fix_indices(Is, Js, (M, N)):
    """This makes the stencil wrap around the grid. It is unclear if this should be
    done, but it allows using a 4-point stencil for all points, even if they
    are on the edge of the grid (otherwise we need to use three points on the
    sides and two in the corners).

    Is and Js are arrays with row- and column-indices, M and N are the grid
    dimensions.
    """
    Is[Is ==  M] = 0
    Is[Is == -1] = M-1
    Js[Js ==  N] = 0
    Js[Js == -1] = N-1
    return (Is, Js)

def laplace(data, mask, eps1, eps2, initial_guess='mean', max_iter=10000):
    """laplace solves the Laplace equation using the SOR method with Chebyshev
    acceleration as described in 'Numerical Recipes in Fortran: the art of
    scientific computing' by William H. Press et al -- 2nd edition, section
    19.5.

    data is a 2-d array (computation grid)

    mask is a boolean array; setting mask to 'data == 0', for example, results
         in only modifying points where 'data' is zero, all the other points
         are left as is. Intended use: if in an array the value of -9999.0
         signifies a missing value, then setting mask to 'data == -9999.0'
         fills in all the missing values.

    eps1 is the first stopping criterion: the iterations stop if the norm of
         residual becomes less than eps1*initial_norm, where 'initial_norm' is
         the initial norm of residual. Setting eps1 to zero or a negative
         number disables this stopping criterion.

    eps2 is the second stopping criterion: the iterations stop if the absolute
         value of the maximal change in value between successive iterations is
         less than eps2. Setting eps2 to zero or a negative number disables
         this stopping criterion.

    initial_guess is the initial guess used for all the values in the domain;
         the default is 'mean', i.e. use the mean of all the present values as
         the initial guess for missing values. initial_guess has to be 'mean'
         or a number.

    max_iter is the maximum number of iterations allowed. The default is 10000.
    """
    dimensions = data.shape
    rjac = rho_jacobi(dimensions)
    i, j = indices(dimensions)
    # This splits the grid into 'odd' and 'even' parts, according to the
    # checkerboard pattern:
    odd  = (i % 2 == 1) ^ (j % 2 == 0) 
    even = (i % 2 == 0) ^ (j % 2 == 0)
    # odd and even parts _in_ the domain:
    odd_part  = zip(i[mask & odd], j[mask & odd])
    even_part = zip(i[mask & even], j[mask & even])
    # relative indices of the stencil points:
    k = array([0, 1, 0, -1])
    l = array([-1, 0, 1, 0])
    parts = [odd_part, even_part]

    if type(initial_guess) == type('string'):
        if initial_guess == 'mean':
            present = array(ones_like(mask) - mask, dtype=bool)
            initial_guess = mean(data[present])
        else:
            print """ERROR: initial_guess of '%s' is not supported (it should be a number or 'mean').
Note: your data was not modified.""" % initial_guess
            return

    data[mask] = initial_guess
    print "Using the initial guess of %10f." % initial_guess

    # compute the initial norm of residual
    initial_norm = 0.0
    for m in [0,1]:
        for i,j in parts[m]:
            Is, Js = fix_indices(i + k, j + l, dimensions)
            xi = sum(data[Is, Js]) - 4 * data[i,j]
            initial_norm += abs(xi)
    print "Initial norm of residual =", initial_norm

    omega = 1.0
    # The main loop:
    for n in arange(max_iter):
        anorm = 0.0
        change = 0.0
        for m in [0,1]:
            for i,j in parts[m]:
                # stencil points:
                Is, Js = fix_indices(i + k, j + l, dimensions)
                residual = sum(data[Is, Js]) - 4 * data[i,j]
                delta = omega * 0.25 * residual
                data[i,j] += delta
                
                # record the maximal change and the residual norm:
                anorm += abs(residual)
                if abs(delta) > change:
                    change = abs(delta)
                # Chebyshev acceleration (see formula 19.5.30):
                if n == 1 and m == 1:
                    omega = 1.0/(1.0 - 0.5 * rjac**2)
                else:
                    omega = 1.0/(1.0 - 0.25 * rjac**2 * omega)
        print "max change = %10f, residual norm = %10f" % (change, anorm)
        if (anorm < eps1*initial_norm) or (change < eps2):
            print "Exiting with change=%f, anorm=%f after %d iteration(s)." % (change,
                                                                               anorm, n + 1)
            return
    print "Exceeded the maximum number of iterations."
    return

if __name__ == "__main__":
    from getopt import getopt, GetoptError
    from sys import argv, exit
    from shutil import copyfile, move
    from tempfile import mkstemp
    from os import close
    from time import time
    from pycdf import *

    try:
        opts, args = getopt(argv[1:], "f:v:o:e:i:",
                            ["file=", "variables=", "out_file=",
                             "eps=", "initial_guess="])
        # defaults:
        input_filename = ""
        output_filename = ""
        variables = []
        eps = 1.0
        initial_guess = 'mean'
        for opt, arg in opts:
            if opt in ("-f", "--file"):
                input_filename = arg
            if opt in ("-o", "--out_file"):
                output_filename = arg
            if opt in ("-v", "--variables"):
                variables = arg.split(",")
            if opt in ("-e", "--eps"):
                eps = float(arg)
            if opt in ("-i", "--initial_guess"):
                initial_guess = float(arg)
    except GetoptError:
        print "Incorrect command line arguments. Exiting..."
        exit(-1)

    if input_filename == "":
        print """Please specify the input file name
(using the -f or --file command line option)."""
        exit(-1)
    if variables == []:
        print """Please specify the list of variables to process
(using the -v or --variables command line option)."""
        exit(-1)

    if output_filename == "":
        print """Please specify the output file name
(using the -o or --out_file command line option)."""
        exit(-1)

    print "Creating the temporary file..."
    try:
        (handle, tmp_filename) = mkstemp()
        close(handle) # mkstemp returns a file handle (which we don't need)
        copyfile(input_filename, tmp_filename)
    except IOError:
        print "ERROR: Can't create %s, Exiting..." % tmp_filename

    try:
        nc = CDF(tmp_filename, NC.WRITE)
        nc.automode()
    except CDFError, message:
       print message
       print "Note: %s was not modified." % output_filename
       exit(-1)

    t_zero = time()
    for name in variables:
        print "Processing %s..." % name
        try:
            var = nc.var(name)
            data = var.get()

            # This happens only if the current variable depends on time; in
            # that case we take the first time-slice only.
            if len(data.shape) == 3:
                data = data[0]

            attributes = ["valid_range", "valid_min", "valid_max",
                          "_FillValue", "missing_value"]
            adict = {}
            print "Reading attributes..."
            for attribute in attributes:
                try:
                    print "* %15s -- " % attribute,
                    attr = var.attr(attribute).get()
                    adict[attribute] = attr
                    print "found"
                except:
                    print "not found"

            if adict.has_key("valid_range"):
                range = adict["valid_range"]
                mask = ((data >= range[0]) & (data <= range[1]))
                print "Using the valid_range attribute; range = ", range

            elif adict.has_key("valid_min") and adict.has_key("valid_max"):
                valid_min = adict["valid_min"]
                valid_max = adict["valid_max"]
                mask = ((data >= valid_min) & (data <= valid_max))
                print """Using valid_min and valid_max attributes.
valid_min = %10f, valid_max = %10f.""" % (valid_min, valid_max)

            elif adict.has_key("valid_min"):
                valid_min = adict["valid_min"]
                mask = data >= valid_min
                print "Using the valid_min attribute; valid_min = %10f" % valid_min

            elif adict.has_key("valid_max"):
                valid_max = adict["valid_max"]
                mask = data <= valid_max
                print "Using the valid_max attribute; valid_max = %10f" % valid_max

            elif adict.has_key("_FillValue"):
                fill_value = adict["_FillValue"]
                if fill_value <= 0:
                    mask = data <= fill_value + 2*finfo(float).eps
                else:
                    mask = data >= fill_value - 2*finfo(float).eps
                print "Using the _FillValue attribute; _FillValue = %10f" % fill_value

            elif adict.has_key("missing_value"):
                missing = adict["missing_value"]
                mask = abs(data - missing) < 2*finfo(float).eps
                print """Using the missing_value attribute; missing_value = %10f
Warning: this attribute is deprecated by the NUG.""" % missing

            else:
                print "No missing values found. Skipping this variable..."
                continue

            count = int(sum(mask))
            if count == 0:
                print "No missing values found. Skipping this variable..."
                continue
            print "Filling in %5d missing values..." % count
            t0 = time()
            laplace(data, mask, -1, eps, initial_guess=initial_guess)
            var.put(data)
            print "This took %5f seconds." % (time() - t0)
        except CDFError, message:
            print "ERROR:", message
            print "Note: %s was not modified." % output_filename
            exit(-1)

    print "Processing all the variables took %5f seconds." % (time() - t_zero)
    nc.close()
    try:
        move(tmp_filename, output_filename)
    except:
        print "Error moving %s to %s. Exiting..." % (tmp_filename,
                                                     output_filename)
        exit(-1)
