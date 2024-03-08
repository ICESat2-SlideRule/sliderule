# Citing SlideRule
## Citing SlideRule project
If you use the SlideRule service and/or data products for scientific research resulting in a publication or other report/website with references, please cite the following:

[![DOI](https://joss.theoj.org/papers/10.21105/joss.04982/status.svg)](https://doi.org/10.21105/joss.04982)

>Shean, D., Swinski, J.P., Smith, B., Sutterley, T., Henderson, S., Ugarte, C., Lidwa, E., and T. Neumann (2023). SlideRule: Enabling rapid, scalable, open science for the NASA ICESat-2 mission and beyond. Journal of Open Source Software, 8(81), 4982, https://doi.org/10.21105/joss.04982

### bibtex
> @article{Shean2023,  
> doi = {10.21105/joss.04982},  
> url = {https://doi.org/10.21105/joss.04982},  
> year = {2023},  
> publisher = {The Open Journal},  
> volume = {8},  
> number = {81},  
> pages = {4982},  
> author = {David Shean and J.p. Swinski and Ben Smith and Tyler Sutterley and Scott Henderson and Carlos Ugarte and Eric Lidwa and Thomas Neumann},  
> title = {SlideRule: Enabling rapid, scalable, open science for the NASA ICESat-2 mission and beyond},  
> journal = {Journal of Open Source Software} } 

## Citing SlideRule data products
Please include a citation for the specific `sliderule` version used to prepare your data products, replacing the X.Y.Z version number below:

[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.4658805.svg)](https://doi.org/10.5281/zenodo.4658805)
>JP Swinski, Eric Lidwa, Tyler Sutterley, Scott Henderson, David Shean, Carlos E. Ugarte, James (Jake) Gearon, Joseph H Kennedy (2023). ICESat2-SlideRule/sliderule: vX.Y.Z. Zenodo. [https://doi.org/10.5281/zenodo.7927340](https://doi.org/10.5281/zenodo.7927340)

We recommend that you document the key processing parameters used for custom ATL06-SR product generation (e.g., segment length, step size, photon classification used, etc.) in your methods section.  Better yet, release your documented, reproducible open code/notebooks, including the full SlideRule API call used to prepare the data!

In the future, we hope to bundle improved metadata with products returned by SlideRule, for improved reproducibility and data provenance.
