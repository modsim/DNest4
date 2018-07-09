# -*- coding: utf-8 -*-

__version__ = "0.2.2"

try:
    __DNEST4_SETUP__
except NameError:
    __DNEST4_SETUP__ = False

if not __DNEST4_SETUP__:
    __all__ = ["DNest4Sampler", "postprocess", "analysis", "my_loadtxt, loadtxt_rows"]

    from . import analysis
    from .sampler import DNest4Sampler
    from .deprecated import postprocess, postprocess_abc
    from .loading import my_loadtxt, loadtxt_rows
    from .utils import rand
    from .utils import randn
    from .utils import randh
    from .utils import wrap

