from setuptools import setup, Extension
from setuptools.command.build_py import build_py as _build_py

# Have to define a new class that is used for the build_py step because build_py expects to find
# asi.py, but that isn't generated until build_ext is run. Normally build_ext is run after 
# build_py. When this class is used for build_py, build_ext is run first. See also the cmdclass
# option added to the setup() function further down in this file.
class build_py(_build_py):
    def run(self):
        self.run_command("build_ext")
        return super().run()

setup(
    name='asi',

    # Versions should comply with PEP440.  For a discussion on single-sourcing
    # the version across setup.py and the project code, see
    # https://packaging.python.org/en/latest/single_source_version.html
    version='0.1.0',

    description='Python wrapper for the ZWO ASI Camera Linux SDK',

    url='https://github.com/seeing-things/zwo/',

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

    install_requires=[
        'numpy',
    ],

    ext_modules=[
        Extension(
            '_asi',
            ['asi.i'],
            swig_opts=['-modern', '-I/usr/include'],
            libraries=['ASICamera2'],
        )
    ],

    py_modules=['asi'],

    cmdclass={'build_py': build_py},
)
