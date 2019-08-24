from setuptools import setup, find_packages, Extension
# To use a consistent encoding
from codecs import open
from os import path

here = path.abspath(path.dirname(__file__))

# Get the long description from the README file
# with open(path.join(here, 'README.md'), encoding='utf-8') as f:
#     long_description = f.read()

setup(
    name='asi',

    # Versions should comply with PEP440.  For a discussion on single-sourcing
    # the version across setup.py and the project code, see
    # https://packaging.python.org/en/latest/single_source_version.html
    version='0.1.0',

    description='Python wrapper for the Linux ZWO ASI Camera SDK',
    # long_description=long_description,

    # url='https://github.com/bgottula/point',

    author='Brett Gottula',
    author_email='bgottula@gmail.com',

    license='MIT',

    # See https://pypi.python.org/pypi?%3Aaction=list_classifiers
    classifiers=[
        'Development Status :: 3 - Alpha',
        'Intended Audience :: Developers',
        'Topic :: Scientific/Engineering :: Astronomy',
        'License :: OSI Approved :: MIT License',
        'Programming Language :: Python :: 3',
    ],

    keywords='astronomy telescopes zwo asi',

    # packages=find_packages(exclude=['contrib', 'docs', 'tests']),

    ext_modules=[
        Extension(
            '_asi',
            ['asi.i'],
            swig_opts=['-modern', '-I../1.14.0715/linux_sdk/include/'],
            include_dirs=['../1.14.0715/linux_sdk/include/'],
            library_dirs=['../1.14.0715/linux_sdk/lib/x64/'],
            extra_objects=['../1.14.0715/linux_sdk/lib/x64/libASICamera2.so.1.14.0715'],
        )
    ],

    py_modules=['asi'],
)
