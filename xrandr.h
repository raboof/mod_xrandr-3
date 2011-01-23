struct xrandr_output_info
{
    int x;
    int y;
    int w;
    int h;
};

extern struct xrandr_output_info** xrandr_init(Display *dpy, char *display_name, int* noutputs);
