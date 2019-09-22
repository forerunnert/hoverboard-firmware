# Python3 code for the implementation of
# Linear extrapolation

# Function to calculate the linear
# extrapolation
def extrapolate(d, x):
    y = (d[0][1] + (x - d[0][0]) /
         (d[1][0] - d[0][0]) *
         (d[1][1] - d[0][1]));

    return y;


# Driver Code

# Sample dataset
d = [[5.2, 350], [10.4, 420], [15.6,520], [20.8 , 600], [38 , 1000]];

# Sample speed value (km/h)
x = 35;

# Finding the extrapolation
print("Value of y at x = %s :" % x,
      extrapolate(d, x));

# This code is contributed by mits
