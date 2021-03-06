/*	LSculpt: Studs-out LEGO? Sculpture

	Copyright (C) 2010 Bram Lambrecht <bram@bldesign.org>

	http://lego.bldesign.org/LSculpt/

	This file (LSculpt.cpp) is part of LSculpt.

	LSculpt is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	LSculpt is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see http://www.gnu.org/licenses/  */

//
// Function definitions for PLY/STL to LDR oriented voxelization
// Comments describing each function are in LSculpt_functions.h
//

#include "LSculpt_functions.h"
#include "LSculpt_obj.h"      // Wavefront OBJ loading
#include <string.h>
#include <limits.h>

//
// ===== Global variables =====
//

// Triangle mesh input:  Rotation of the mesh is done during
// file input.  Scaling and translation is done on the fly
// by scaling and translating the space partitioning cubes
vector<Triangle>       inputmesh;

// Pairing of each space partitioning cube with its location
// in space.  The map allows easy lookup of a cube based
// on its location in space.
map<SpCubeKey, SpCube> cubelist;

// Queue of optimization energies
multiset<SpCubeEnergy> cubeenergy;

// Globals for PLY file reading
vector<SmVector3> vtxs; // temporary global variable for storing vertices

//
// ===== Command line options =====
//

ArgumentSet args = defaultArgs;  // global set of command line arguments, initialized to default values

#ifdef LSCULPT_CONSOLE
int main(int argc, char *argv[])
{
	args = defaultArgs;

	char infile[80] = "", outfile[80] = "";
	load_options(argc, argv, infile, outfile);
	return main_wrapper(infile, outfile, 0);
}
#endif

int main_wrapper(char *infile, char *outfile, void (*progress_cb)(const char *))
{
	bool noerr;
	SmVector3 mn, mx, sz;

	if(args.OPTS_MESSAGE==MESSAGE_ALL) cout << "TIME\t: PROGRESS" << endl;
	if(args.OPTS_MESSAGE==MESSAGE_ALL) cout << now() << "\t: reading input file: " << infile << endl;
	if (progress_cb) progress_cb("Importing Mesh Triangles");

	// Reset global collections - necessary now that main can be called multiple times per execution
	inputmesh.clear();
	cubelist.clear();
	cubeenergy.clear();
  vtxs.clear();

	switch(args.OPTS_FORMAT) {
		case FORMAT_STL:
			noerr = load_triangles_stl(infile);
			break;
        case FORMAT_OBJ:
            noerr = load_triangles_obj(infile);
            break;
		case FORMAT_PLY:
		default:
			noerr = load_triangles_ply(infile);
			break;
	}
	if(args.OPTS_MESSAGE==MESSAGE_ALL) cout << now() << "\t: mesh loaded, " << inputmesh.size() << " triangles" << endl;
	if (progress_cb) progress_cb("Calculating Mesh Bounds");

	if(noerr) {
		// calculate bounding box and normal vectors for triangles
		noerr = mesh_bounds(mn,mx);
	}

	if(noerr) {
		sz = mx - mn;
		int tmp = cout.precision();
		cout.precision(3);
		if(args.OPTS_MESSAGE==MESSAGE_ALL) cout << now() << "\t: output size is " <<
			sz[0] / (args.OPTS_SCALE*VOXEL_WIDTH) << " x " <<
			sz[2] / (args.OPTS_SCALE*VOXEL_WIDTH) << " x " <<
			sz[1] / (args.OPTS_SCALE*VOXEL_WIDTH) << " studs" << endl;
		cout.precision(tmp);
		if (progress_cb) progress_cb("Partitioning Space");
		partition_space();
		if(args.OPTS_MESSAGE==MESSAGE_ALL) cout << now() << "\t: mesh partitioned into " << cubelist.size() << " cubes" << endl;
    if(args.OPTS_MAXITER >= 0 && args.OPTS_PLATES) {
			if (progress_cb) progress_cb("Computing Normals");
			compute_cube_normals();
			if(args.OPTS_MESSAGE==MESSAGE_ALL) cout << now() << "\t: computed initial cube orientations" << endl;
			if (progress_cb) progress_cb("Initializing Voxels");
			init_voxels();
			if(args.OPTS_MESSAGE==MESSAGE_ALL) cout << now() << "\t: surfaces found in " << cubelist.size() << " cubes" << endl;
		}
    if(args.OPTS_MAXITER > 0 && args.OPTS_PLATES) {
			identify_neighbors();
			initialize_energy();
			if(args.OPTS_MESSAGE==MESSAGE_ALL) cout << now() << "\t: optimization started" << endl;
			if (progress_cb) progress_cb("Optimizing Voxels");
			unsigned int it = optimize_voxels();
			if(args.OPTS_MESSAGE==MESSAGE_ALL) cout << now() << "\t: optimization finished in " << it << " iterations" << endl;
		}
		if (progress_cb) progress_cb("Building Temporary Model");
		save_ldraw(outfile);
		if(args.OPTS_MESSAGE==MESSAGE_ALL) cout << now() << "\t: output file " << outfile << " saved" << endl;
	}	

	return (noerr) ? EXIT_SUCCESS : EXIT_FAILURE;
}

float now()
{
    return float(clock()) / CLOCKS_PER_SEC;
}

ArgumentSet getDefaultArgumentSet()
{
	return defaultArgs;
}

void setArgumentSet(ArgumentSet localArgs)
{
	args = localArgs;
}

void load_options(int argc, char*argv[], char* in, char* out)
{
	char message[80] = "";
	char arg[80] = "";

	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			switch (argv[i][1]) {
				case 'k':
					args.OPTS_COLOR = atoi(argv[++i]);
					break;
				case 'a':
					args.OPTS_PART = atoi(argv[++i]);
					break;
				case 'i':
          strcpy(arg,argv[++i]);
					strupper(arg);
					if (strcmp(arg,"PLY")==0)
						args.OPTS_FORMAT = FORMAT_PLY;
					else if (strcmp(arg,"STL")==0)
						args.OPTS_FORMAT = FORMAT_STL;
                    else if (strcmp(arg,"OBJ")==0)
                        args.OPTS_FORMAT = FORMAT_OBJ;
					else {
            strcpy(message, "ERROR: Unknown input file format: ");
            strcat(message, arg);
					}
					break;
				case 'u':
          strcpy(arg,argv[++i]);
					strupper(arg);
					if (strcmp(arg,"MM")==0)
						args.OPTS_SCALE *= UNIT_LDU_MM;
					else if (strcmp(arg,"CM")==0)
						args.OPTS_SCALE *= UNIT_LDU_CM;
					else if (strcmp(arg,"M")==0)
						args.OPTS_SCALE *= UNIT_LDU_M;
					else if (strcmp(arg,"IN")==0)
						args.OPTS_SCALE *= UNIT_LDU_IN;
					else if (strcmp(arg,"FT")==0)
						args.OPTS_SCALE *= UNIT_LDU_FT;
					else if (strcmp(arg,"STUD")==0)
						args.OPTS_SCALE *= UNIT_LDU_ST;
					else if (strcmp(arg,"LDU")==0);
					else {
            strcpy(message, "ERROR: Unknown units: ");
            strcat(message, arg);
					}
					break;
				case 'o':
					args.OPTS_OFFSET[0] = atof(argv[++i]);
					args.OPTS_OFFSET[1] = atof(argv[++i]);
					args.OPTS_OFFSET[2] = atof(argv[++i]);
					break;
				case 'c':
					args.OPTS_CENTER = true;
					break;
				case 'b':
					setStudsUpBaseArg(&args, atoi(argv[++i]));
					break;
				case 'd':
          strcpy(arg,argv[++i]);
					strupper(arg);
					if (strcmp(arg,"Z")==0) args.OPTS_UP = UP_Z;
					break;
				case 'n':
					args.OPTS_STUDSUP = true;
					break;
				case 'f':
					args.OPTS_FIT = atof(argv[++i]);
					break;
				case 'r':
					args.OPTS_ROT = atof(argv[++i]);
					args.OPTS_ROT_SIN = sin(args.OPTS_ROT*PI/180.0);
					args.OPTS_ROT_COS = cos(args.OPTS_ROT*PI/180.0);
					break;				
				case 's':
					args.OPTS_SCALE /= atof(argv[++i]);
					break;
				case 'h':
					usage();
					exit(0);
					break;
				case 'p':
					args.OPTS_MAXITER = atoi(argv[++i]);
					break;
				case 'q':
					args.OPTS_MAXITER = 0;
					break;
				case 'v':
					args.OPTS_MESSAGE = MESSAGE_ALL;
					break;
				case 'w':
					args.OPTS_MESSAGE = MESSAGE_NONE;
					break;
				case 'y':
					args.OP_ORN = atoi(argv[++i]);
					args.OP_DIR = atoi(argv[++i]);
					args.OP_NBR = atoi(argv[++i]);
					break;
				case 'z':
					args.OP_THN = atoi(argv[++i]);
					args.OP_NCT = atoi(argv[++i]);
					args.OP_SOR = atoi(argv[++i]);
					args.OP_BAK = atoi(argv[++i]);
					args.OP_BKO = atoi(argv[++i]);
					break;
				case 'e':
					args.OPTS_NOFILL = true;
					break;
				default:
          strcpy(message, "ERROR: Unknown option: ");
          strcat(message, argv[i]);
					break;
			}
			if (strcmp(message, "")!=0) {
				usage();
				cout << endl << message << endl;
				exit(1);
			}
		} else if (strcmp(in,"")==0) {
      strcpy(in,argv[i]);
		} else if (strcmp(out,"")==0) {
      strcpy(out,argv[i]);
		} else {
			usage();
			cerr << endl << "ERROR: Unknown option: " << argv[i] << endl;
			exit(1);
		}
	}

	// error check options:
	if (strcmp(in,"")==0) {
		usage();
		cerr << endl << "ERROR: No input file specified." << endl;
		exit(1);
	}
	if(args.OPTS_SCALE <= 0.0 || args.OPTS_FIT < 0.0) {
		if(args.OPTS_MESSAGE) cerr << "ERROR: Scale factor must be positive" << endl;
		exit(1);
	}

	setOutFile(args, in, out);
	setFileFormat(&args, in);
}

void setStudsUpBaseArg(ArgumentSet *localArgs, int value)
{
	localArgs->OPTS_BASE = (value/(SPCUBE_WIDTH/VOXEL_HEIGHT))
		+ (value%(SPCUBE_WIDTH/VOXEL_HEIGHT)>0 ? 1 : 0);
}

void setOutFile(ArgumentSet localArgs, char *in, char *out)
{
	if(strcmp(out,"")==0)
	{
		if (!localArgs.OPTS_FORMAT)
      strncpy(out,in,strrchr(in,'.')-in);
		else
      strcpy(out,in);

		if (localArgs.OPTS_PART > 1)
      strcat(out,".mpd");
		else
      strcat(out,".ldr");
	}
}

void setFileFormat(ArgumentSet *localArgs, char *in)
{
	char arg[80] = "";

	// if no input format specified, get it from the file extension
	if(!localArgs->OPTS_FORMAT) {
		strcpy(arg,strrchr(in,'.') + 1);
		strupper(arg);
		if (strcmp(arg,"PLY")==0)
			localArgs->OPTS_FORMAT = FORMAT_PLY;
		else if (strcmp(arg,"STL")==0)
			localArgs->OPTS_FORMAT = FORMAT_STL;
        else if (strcmp(arg,"OBJ")==0)
            localArgs->OPTS_FORMAT = FORMAT_OBJ;
		else {
			localArgs->OPTS_FORMAT = FORMAT_PLY;
			if(localArgs->OPTS_MESSAGE) cerr << "WARNING: Could not determine file format, assuming PLY." << endl;
		}
	}
}

void strupper(char *str)
{
	while(*str) {
		*str = toupper(*str);
		str++;
	}
}

void usage()
{
    cout
    << "Usage: LSculpt [OPTIONS] inputfile [outputfile]"                               << endl
    << "inputfile     is a mesh of triangles in a supported format."				   << endl
    << "outputfile    is the desired filename for the LDraw file.  If no filename is"  << endl
    << "              provided, the inputfile will receive an .ldr or .mpd extension." << endl
    << "OPTIONS may appear anywhere in the command line.  OPTIONS are:"                << endl
	<< endl
    << " -i  format   force input format for mesh.  Default is from file extension."   << endl
    << "              format = [ply, stl]"                                             << endl
    << " -u  unit     units for mesh, offset, or fit."                                 << endl
    << "              unit = [mm, cm, m, in, ft, stud, ldu (default)]"                 << endl
    << " -o  x y z    offset the origin to this location."                             << endl
    << " -c           re-center mesh at the origin."                                   << endl
	<< " -d  up       up axis: up = [y (default), z]."                                 << endl
	<< " -r  angle    rotate mesh about the 'up' axis by angle degrees."               << endl
    << " -f  length   fit the mesh so the longest side is 'length' units."             << endl
    << " -s  factor   scale factor for mesh"                                           << endl
	<< endl
	<< " -b  level    use a base of studs up pieces for the bottom 'level' layers"     << endl
	<< " -n           use only studs up pieces for the first attempt"                  << endl
	<< " -e           create an empty shell (use with disconnected or overlapping"     << endl
	<< "              surfaces that otherwise result in lots of artifacts"             << endl
	<< " -a  part     choose which part to use in LDraw file. Choose from:"            << endl
	<< "              0:  1 x 1 plate (default)"                                       << endl
	<< "              1:  1 x 1 tile"                                                  << endl
	<< "              2:  simplifed 1 x 1 plate for fast rendering"                    << endl
	<< "              3:  simplifed 1 x 1 tile for fast rendering"                     << endl
	<< " -k  color    choose a color option for the output file. Choose from:"         << endl
	<< "              0:  default color"                                               << endl
	<< "              1:  color pieces in layers"                                      << endl
	<< "              2:  color pieces according to their orientation"                 << endl
	<< endl
    << " -p  iter     optimize orientation of studs for 'iter' iterations"             << endl
    << " -q           quick: same as -p 0"                                             << endl
	<< " -y  a b c    optimization options (defaults are given in []):"                << endl
	<< "              a [0.25] amount to match piece orientation to surface normal"    << endl
	<< "              b [0.25] amount to make studs point outward from surface"        << endl
	<< "              c [0.50] amount to match piece orientation to neighbors"         << endl
	<< " -z  a to e   optimization options (values may be negative or positive):"      << endl
	<< "              a [0.00] extra weight for thin neighbors"                        << endl
	<< "              b [0.00] extra weight for lonely neighbors"                      << endl
	<< "              c [0.00] extra weight for neighbors pointing the opposite way"   << endl
	<< "              d [0.00] extra weight for neighbors in front or behind"          << endl
	<< "              e [0.00] extra weight for neighbors which match c AND d"         << endl
	<< endl
    << " -h           show this message."                                              << endl
	<< " -v           verbose output"                                                  << endl
    << " -w           suppress all warning messages"                                   << endl;
}

bool load_triangles_obj(char *fname)
{
  std::ifstream ifstream;

  ifstream.open(fname);
  if (!ifstream.is_open()) {
    std::cerr << "OBJ: " << fname << ": " << "no such file or directory" << "\n";
    return false;
  }

  class obj_loader load_obj;
  return load_obj.convert(ifstream, inputmesh, args);
}

//
// PLY input code adapted from source code available at
// http://www.cs.princeton.edu/~diego/professional/rply
//
bool load_triangles_ply(char *fname)
{
	long nvertices, ntriangles;

	// Open PLY file
    p_ply ply = ply_open(fname, NULL, NULL, NULL);

	// Error check file
	if (!ply) {
		cerr << "ERROR: Invalid or unreadable PLY file.";
		return false;
	}

	if (!ply_read_header(ply)) {
		cerr << "ERROR: Invalid or unreadable PLY file.";
		return false;
	}

	// call vertex input callback function for each coordinate
	nvertices = ply_set_read_cb(ply, "vertex", "x", myply_vertex_cb, NULL, 0);
	ply_set_read_cb(ply, "vertex", "y", myply_vertex_cb, NULL, 1);
	ply_set_read_cb(ply, "vertex", "z", myply_vertex_cb, NULL, 2);

	// call face input callback function for each list of faces
	ntriangles = ply_set_read_cb(ply, "face", "vertex_indices", myply_face_cb, NULL, 0);

	// call face input callback function for each list triangle strip
  ntriangles += ply_set_read_cb(ply, "tristrips", "vertex_indices", myply_face_cb, NULL, 1);

  if (nvertices <= 0 || ntriangles <= 0)
    cerr << "WARNING: No polygons found in Mesh file.\n";

	// read the actual file in
	if (!ply_read(ply))
		cerr << "WARNING: Problems reading Mesh file - 3D model may be incorrect.\n";
	ply_close(ply);

	return true; //success
}

// PLY file: read vertex callback function
int myply_vertex_cb(p_ply_argument argument) {
    long coord;
    ply_get_argument_user_data(argument, NULL, &coord);

	// new x coordinate => new vertex
	if (coord == 0)
		vtxs.push_back(SmVector3());

	// store vertex coordinate
	switch (args.OPTS_UP) {
		case UP_Z:
			switch (coord) {
				case 0:	vtxs.back()[0] = ply_get_argument_value(argument); break;
				case 1:	vtxs.back()[2] = -ply_get_argument_value(argument); break;
				case 2:	vtxs.back()[1] = ply_get_argument_value(argument); break;
			}
			break;
		default:
			vtxs.back()[coord] = ply_get_argument_value(argument);
			break;
	}	
    return 1;
}

// PLY file: read triangle callback function
int myply_face_cb(p_ply_argument argument) {
	long length, face_vertex_index, vtxs_length, is_strip;
	static long v_index_first, v_index_prev, v_index_last, strip_offset=0;
	static bool have_bad_vertex_err = false;

	ply_get_argument_property(argument, NULL, &length, &face_vertex_index);
	ply_get_argument_user_data(argument, NULL, &is_strip);

	// reset strip_offset for new strips or faces.
	if (face_vertex_index == 0 || face_vertex_index == -1) {
		strip_offset = 0;
	}

	vtxs_length = vtxs.size();
	face_vertex_index -= strip_offset;

	switch (face_vertex_index) {
		case -1:
			// new vertex list
			break;
		case 0:
			v_index_first = ply_get_argument_value(argument);
			break;
		case 1:
			v_index_prev = ply_get_argument_value(argument);
			break;
		default:
			v_index_last = ply_get_argument_value(argument);

			if (v_index_last == -1) { // start a new strip
				strip_offset += face_vertex_index + 1;
			} else if ( // error checking
				v_index_first < 0 || v_index_first >= vtxs_length || // out of bounds
				v_index_prev  < 0 || v_index_prev  >= vtxs_length ||
				v_index_last  < 0 || v_index_last  >= vtxs_length )
			{
				if (!have_bad_vertex_err) {
					have_bad_vertex_err = true;
					cerr << "WARNING: PLY file contains an out of bounds vertex.\n";
				}
				return 1;
			} else {
				if( v_index_first == v_index_prev  ||  // degenerate triangle
					v_index_prev  == v_index_last  ||
					v_index_last  == v_index_first ) {
				} else {

					inputmesh.push_back(Triangle());

					// store the vertex in the current triangle
					inputmesh.back().v[0] = roty(vtxs.at(v_index_first));
					inputmesh.back().v[1] = roty(vtxs.at(v_index_prev));
					inputmesh.back().v[2] = roty(vtxs.at(v_index_last));
				}
			}

			if (is_strip && !(face_vertex_index & 1) ) { // index is odd
				v_index_first = v_index_last;
			} else {
				v_index_prev = v_index_last;
			}

			break;
	}

	return 1;
}

inline SmVector3 roty(SmVector3 pt)
{
	return SmVector3(
		args.OPTS_ROT_COS*pt[0] + args.OPTS_ROT_SIN*pt[2],
		pt[1],
		-args.OPTS_ROT_SIN*pt[0] + args.OPTS_ROT_COS*pt[2]
		);
}


// STL file

#define STL_HEADER 80
#define STL_FLOAT   4
#define STL_LONG    4
#define STL_INT     2

bool load_triangles_stl(char *fname)
{
	long begin, end;
	unsigned long nfacets;
	unsigned char i;
	float vtx[3];

	ifstream stl(fname, ios::in | ios::binary);
	if(stl.good()) {
		// skip the 80 byte header
		stl.seekg(STL_HEADER, ios::beg);
		// get the number of triangles
		stl.read((char *) &nfacets, STL_LONG);
		// get the file size
		begin = stl.tellg();
		stl.seekg(0, ios::end);
		end = stl.tellg();
		// check if the file size is correct
		// for a binary STL file
		if(nfacets * 50 == (unsigned long)(end-begin)) {
			// binary file
			stl.seekg(STL_HEADER + STL_LONG - STL_INT, ios::beg);
			for (;nfacets > 0; nfacets--) {
				inputmesh.push_back(Triangle());
				// skip normal, attribute byte count
				stl.seekg(STL_INT + 3*STL_FLOAT, ios::cur);
				for (i = 0; i < 3; i++) {
					stl.read((char *) &vtx[0],STL_FLOAT);
					stl.read((char *) &vtx[1],STL_FLOAT);
					stl.read((char *) &vtx[2],STL_FLOAT);
					switch (args.OPTS_UP) {
						case UP_Z:
							inputmesh.back().v[i] = roty(SmVector3(vtx[0],vtx[2],-vtx[1]));
							break;
						default:
							inputmesh.back().v[i] = roty(SmVector3(vtx[0],vtx[1],vtx[2]));
							break;
					}
				}
			}
			stl.close();
			return true;
		} else {
			// ascii file
			stl.close();
			return load_triangles_stla(fname);
		}
	} else {
		return false;
	}

}

bool load_triangles_stla(char *fname)
{
	char str[80];
	ifstream stl(fname, ios::in);
	unsigned char i;
	float vtx[3];

	while(stl.good()) {
		stl >> str;
		if(strstr(str, "facet") && !strstr(str, "endfacet")) {
			inputmesh.push_back(Triangle());
			i = 0;
			while(i < 3 && !strstr(str, "endfacet") && stl.good()) {
				stl >> str;
				if(strstr(str, "vertex")) {
					stl >> vtx[0] >> vtx[1] >> vtx[2];
					switch (args.OPTS_UP) {
						case UP_Z:
							inputmesh.back().v[i] = roty(SmVector3(vtx[0],vtx[2],-vtx[1]));
							break;
						default:
							inputmesh.back().v[i] = roty(SmVector3(vtx[0],vtx[1],vtx[2]));
							break;
					}
					i++;
				}
			}
			if (i < 3) return false;
		}
	}
	return true;
}

bool mesh_bounds(SmVector3 &mn, SmVector3 &mx)
{
	SmVector3 tmin, tmax, sz;

	// calculate the normals, bounding boxes for each triangle
	for(vector<Triangle>::iterator i = inputmesh.begin(); i != inputmesh.end(); i++) {
		(*i).computenormal();
		(*i).computebounds(tmin, tmax);
		
		if(i == inputmesh.begin()) {
			mn = tmin;
			mx = tmax;
		} else {
			// update bounding box for total mesh
			mn.minSet(tmin);
			mx.maxSet(tmax);
		}
	}

	// size of the mesh
	sz = mx - mn;
	// longest edge

    args.meshsize = max(sz[0],max(sz[1],sz[2]));

	// if fitting is turned on, adjust the scale factor for SpCube
    if(args.OPTS_FIT > 0.0) args.OPTS_SCALE *= args.meshsize/args.OPTS_FIT;

	// if centering is turned on, adjust the offset for the mesh
	if(args.OPTS_CENTER) args.OPTS_OFFSET -= 0.5*(mx+mn);
	// check if the mesh is too big in + direction
	sz = mx + args.OPTS_OFFSET;

    args.meshsize = max(sz[0],max(sz[1],sz[2]));
    if(args.meshsize / (SPCUBE_WIDTH*args.OPTS_SCALE) > (1 << (sizeof(SpCubeCoord)*CHAR_BIT-1))) {
		if(args.OPTS_MESSAGE) cerr << "ERROR: The mesh is larger than the maximum allowed stud length." << endl
                        << "  Try re-centering the mesh at the origin, or check your scale settings" << endl;
		return false;
	}
	// check if the mesh is too big in - direction
	sz = mn + args.OPTS_OFFSET;
    args.meshsize = -min(sz[0],min(sz[1],sz[2]));
    if(args.meshsize / (SPCUBE_WIDTH*args.OPTS_SCALE) > (1 << (sizeof(SpCubeCoord)*CHAR_BIT-1))) {
		if(args.OPTS_MESSAGE) cerr << "ERROR: The mesh is larger than the maximum allowed stud length." << endl
                        << "  Try re-centering the mesh at the origin, or check your scale settings"  << endl;
		return false;
	}

	return true;  // success
}

void partition_space()
{
	SpCube      cube;
	SpCubeKey   mn, mx, k;
	SmVector3   loc;
	double      scale = 1.0 / (args.OPTS_SCALE * SPCUBE_WIDTH),
		        pad = double(SPCUBE_PAD) / SPCUBE_WIDTH;

	for(vector<Triangle>::iterator i = inputmesh.begin(); i != inputmesh.end(); i++) {
		loc = (*i).mx + args.OPTS_OFFSET;
		mx[0] = ceil(loc[0] * scale + pad);
		mx[1] = ceil(loc[1] * scale + pad);
		mx[2] = ceil(loc[2] * scale + pad);

		loc = (*i).mn + args.OPTS_OFFSET;
		mn[0] = floor(loc[0] * scale - pad);
		mn[1] = floor(loc[1] * scale - pad);
		mn[2] = floor(loc[2] * scale - pad);

		mx[0] += (mx[0]==mn[0]) ? 1 : 0;
		mx[1] += (mx[1]==mn[1]) ? 1 : 0;
		mx[2] += (mx[2]==mn[2]) ? 1 : 0; 

		for(k[0] = mn[0]; k[0] < mx[0]; k[0]++) {
			for(k[1] = mn[1]; k[1] < mx[1]; k[1]++) {
				for(k[2] = mn[2]; k[2] < mx[2]; k[2]++) {
					cubelist[k].tlist.push_back(&(*i));
				}
			}
		}
	}
}

void compute_cube_normals()
{
	SmVector3 nabs, navg;
	map<SpCubeKey, SpCube>::iterator c;
	vector<Triangle *>::iterator t;
	double maxdir;
	unsigned char orient;
	bool neg;

	for(c = cubelist.begin(); c != cubelist.end(); c++) {

		// Clear all flags
		(*c).second.flags = 0;

		// sum the area weighted normal vectors for each triangle.
		// also sum the abs value of the normal vectors
		nabs = SmVector3(0.0);
		navg = SmVector3(0.0);

		for(t = (*c).second.tlist.begin(); t != (*c).second.tlist.end(); t++) {
			nabs += abs((*t)->an);
			navg += (*t)->an;
		}

		// if sum(abs) > abs(sum) for any component then there are
		// components pointing in opposite directions.
		if(nabs[0] > fabs(navg[0]))
			(*c).second.markthin(0);
		if(nabs[1] > fabs(navg[1]))
			(*c).second.markthin(1);
		if(nabs[2] > fabs(navg[2]))
			(*c).second.markthin(2);

		// pick the biggest abs value direction
		maxdir = max(nabs);
		orient = (maxdir == nabs[0]) ? 0 : ((maxdir == nabs[1]) ? 1 : 2);

		(*c).second.orientset(orient);

		// Recompute the average normal vector if the
		// biggest direction is a "thin" direction
		if( (*c).second.isthin(orient) ) {
			neg = (navg[orient] < 0.0);
			navg = SmVector3(0.0);
			for(t = (*c).second.tlist.begin(); t != (*c).second.tlist.end(); t++) {
				if ( (!neg && (*t)->an[orient] < 0.0) ||
					( neg && (*t)->an[orient] >= 0.0) ) {
					navg -= (*t)->an;
				} else {
					navg += (*t)->an;
				}
			}
		}

		normalize(navg);
		(*c).second.avgnormal = navg;
	}
}

void init_voxels()
{
	map<SpCubeKey, SpCube>::iterator c, e;
	vector<Triangle *>::iterator t;
	unsigned char orient;

	for(c = cubelist.begin(); c != cubelist.end();) {
		
		orient = (*c).second.orientget();

		// keep positive direction for base:
		if(args.OPTS_BASE>0 && orient == 1 && (*c).first.loc[1] < (*cubelist.begin()).first.loc[1] + args.OPTS_BASE);
		// otherwise check to see if direction should be switched.
		else if((*c).second.avgnormal[orient] < 0.0)
			(*c).second.orientneg();

		if(args.OPTS_STUDSUP) {
			orient = 1;
			(*c).second.orientset(1);
			(*c).second.orientpos();
		}

		voxelize((*c).first,&((*c).second));

		// if no voxels added, reorient and try again
		if((*c).second.voxels[orient] == 0) {
			orient = (orient+1) % 3;
			(*c).second.orientset(orient);
			voxelize((*c).first,&((*c).second));
			// check again
			if((*c).second.voxels[orient] == 0) {
				orient = (orient+1) % 3;
				(*c).second.orientset(orient);
				voxelize((*c).first,&((*c).second));
				// check last time
				if((*c).second.voxels[orient] == 0) {
					// delete cube
					e = c;
					c++; // can't increment iterator if the element is erased,
					     // so increment the iterator first.
					cubelist.erase(e);
					continue;
				}
			} else {
				(*c).second.voxels[(orient+1)%3] = 0;
			}
		} else {
			(*c).second.voxels[(orient+1)%3] = 0;
			(*c).second.voxels[(orient+2)%3] = 0;
		}

		if(args.OPTS_STUDSUP) (*c).second.orientset(1);
		c++;
	}
}

void voxelize(SpCubeKey loc, SpCube *cubeptr)
{
	unsigned char i, j, k;
	unsigned char d, e, f;
	vector<Triangle *>::iterator t;

	VoxelList fill;

	SmVector3 pt, llc;
	set<Hit> intersections;
	set<Hit>::iterator s;

	d = cubeptr->orientget();
	e = (d+1) % 3;
	f = (d+2) % 3;

	// minimum corner of cube
	llc = SmVector3(loc[0],loc[1],loc[2])*args.OPTS_SCALE*SPCUBE_WIDTH - args.OPTS_OFFSET;

	// initialize to empty;
	cubeptr->voxels[d] = 0;

	for (i = 0; i < SPCUBE_WIDTH / VOXEL_WIDTH; i++) {
		pt[e] = llc[e] + (2*i+1)*args.OPTS_SCALE*VOXEL_WIDTH/2;
		for (j = 0; j < SPCUBE_WIDTH / VOXEL_WIDTH; j++) {
			pt[f] = llc[f] + (2*j+1)*args.OPTS_SCALE*VOXEL_WIDTH/2;
			intersections.clear();
			for (t = cubeptr->tlist.begin(); t != cubeptr->tlist.end(); t++) {
				// check the triangle for intersection
				if(recordintersect((*t), pt, d, &intersections) && !cubeptr->isthin(d)) break;
			}

			if(intersections.empty() && !args.OPTS_NOFILL){
				if(!intersectneighbors(loc, pt, d, &intersections, true))
					intersectneighbors(loc, pt, d, &intersections, false);
			}

			// iterate through the list of intersections
			// compute fill between intersections
			if(!intersections.empty()) {
				k = 0; fill = 0;
				if(args.OPTS_NOFILL) {
          for(s = intersections.begin(); s != intersections.end(); s++) {
            // convert the intersection into a position in the cube
						k = (((*s).position+SPCUBE_PAD*args.OPTS_SCALE - llc[d]) / VOXEL_HEIGHT / args.OPTS_SCALE - 0.5);
            // if the position is inside the cube, turn on that voxel
						if( k < SPCUBE_WIDTH/VOXEL_HEIGHT )
							fill |= 1 << k;
					}
				} else {
					for(s = intersections.begin(); s != intersections.end(); s++) {
            for (;k < SPCUBE_WIDTH/VOXEL_HEIGHT && (k+1)*VOXEL_HEIGHT*args.OPTS_SCALE + llc[d] < (*s).position+SPCUBE_PAD*args.OPTS_SCALE; k++) {
							if((*s).inside) fill |= 1 << k;
						}           

            // if the previous for loop ended because the voxel position was past
            // the current intersection, and there is a previous intersection in the cube
            if(k < SPCUBE_WIDTH/VOXEL_HEIGHT && (s != intersections.begin())) {
              // look at the previous intersection
              s--;
              // and check if that previous intersection happened within the same voxel position as the current intersection
              if( (k+1)*VOXEL_HEIGHT*args.OPTS_SCALE + llc[d] >= (*s).position+SPCUBE_PAD*args.OPTS_SCALE &&
                   k*VOXEL_HEIGHT*args.OPTS_SCALE + llc[d] < (*s).position+SPCUBE_PAD*args.OPTS_SCALE) {

                // if so, there are two very closely spaced intersections, so a voxel should be placed
                if((*(++s)).inside) fill |= 1 << k;

              } else {

                // go back to the current intersection
                s++;
              }
            }
					}
					// finish the fill after the last intersection
					for (; k < SPCUBE_WIDTH/VOXEL_HEIGHT && (k+1)*VOXEL_HEIGHT*args.OPTS_SCALE + llc[d] >= (*intersections.rbegin()).position+SPCUBE_PAD*args.OPTS_SCALE; k++) {
						if(!(*intersections.rbegin()).inside) fill |= 1 << k;
					} 
				}
				cubeptr->voxels[d] |= (fill << (i*SPCUBE_WIDTH/VOXEL_WIDTH*SPCUBE_WIDTH/VOXEL_HEIGHT + j*SPCUBE_WIDTH/VOXEL_HEIGHT));
			}
		}
	}
}

inline bool recordintersect(Triangle *t, SmVector3 &pt, const unsigned char &d, set<Hit> *hitlist)
{
	Hit hit;
	if(t->un[d] != 0 && t->intersect(pt,d)) {
		hit.position = pt[d];
		hit.inside = (t->un[d] > 0);
		hitlist->insert(hitlist->begin(),hit);
		return true;
	}
	return false;
}

bool intersectneighbors(SpCubeKey loc, SmVector3 &pt, const unsigned char d, set<Hit> *hitlist, bool plus)
{
	vector<Triangle *>::iterator t;
	map<SpCubeKey,SpCube>::iterator c;

	loc[d] += plus ? 1 : -1;

	if((c = cubelist.find(loc)) != cubelist.end()) {
		for (t = (*c).second.tlist.begin(); t != (*c).second.tlist.end(); t++) {
			if(recordintersect((*t), pt, d, hitlist) && !(*c).second.isthin(d)) break;
		}
		if(hitlist->empty()) {
			return intersectneighbors(loc, pt, d, hitlist, plus);
		} else { 
			return true;
		}
	} else {
		return false;
	}		
}

void identify_neighbors()
{
	map<SpCubeKey, SpCube>::iterator c;
	SpCubeKey p, q;
	unsigned char d;

	for(c = cubelist.begin(); c != cubelist.end(); c++) {
		p = (*c).first;
		(*c).second.neighborclear();
		for(d = 0; d < 3; d++) {			
			p[d] += 1;
			if(cubelist.find(p) != cubelist.end())
				(*c).second.neighboron(d,true);
			p[d] -= 2;
			if(cubelist.find(p) != cubelist.end())
				(*c).second.neighboron(d,false);
			p[d] += 1;
		}
	}
}

inline SpCube *get_neighbor(SpCubeKey loc, unsigned char n) {
	map<SpCubeKey, SpCube>::iterator c;
	loc[n/2] += (n % 2) ? -1 : 1;
	return &cubelist[loc];
}
void initialize_energy()
{
	map<SpCubeKey, SpCube>::iterator c;
	SpCube *neighbors[7];
	unsigned char i;

	// initialize list
	for (c = cubelist.begin(); c != cubelist.end(); c++) {
		neighbors[0] = &((*c).second);
		for (i=0; i<6; i++) {
			neighbors[i+1] = ((*c).second.isneighboron(i)) ? 
				(get_neighbor((*c).first,i)) :
				((SpCube *) 0);
		}
		(*c).second.energy = cubeenergy.end();
		update_energy(neighbors, (*c).first);
	}
}

unsigned int optimize_voxels()
{
	SpCube *c, *neighbors[7];
	SpCubeKey loc;
	unsigned char n, i;
	int j;

	for (j = 0; j < args.OPTS_MAXITER && !cubeenergy.empty(); j++) {

		// pop of the cube that reduces the energy the most
		loc = (*cubeenergy.begin()).cube;
		
		// identify the cube and switch its orientation
		c = &cubelist[loc];
		c->orientset(((SpCubeEnergy)*cubeenergy.begin()).orientget());
		if (((SpCubeEnergy)*cubeenergy.begin()).isorientneg()) 
			c->orientneg();
		else
			c->orientpos();
		if (c->voxels[c->orientget()] == 0)
			voxelize(loc, c);

		// delete it from the list
		cubeenergy.erase(cubeenergy.begin());
		c->energy = cubeenergy.end();

		// identify cube nearest neighbors
		for (n=0; n<6; n++) {
			if(c->isneighboron(n)) {
				loc[n/2] += (n % 2) ? -1 : 1;
				neighbors[0] = &cubelist[loc];
				// get neighbor's neighbors
				for (i=0; i<6; i++) {
					neighbors[i+1] = (neighbors[0]->isneighboron(i)) ? 
						(get_neighbor(loc,i)) :
						((SpCube *) 0);
				}
				update_energy(neighbors, loc);
				loc[n/2] -= (n % 2) ? -1 : 1;
			}
		}
	}
	return j;
}

void update_energy(SpCube *cubeptr[7], SpCubeKey loc)
{
	SpCubeEnergy ce;
	double emin, enow, e;
	ce.orientset(0);
	ce.orientpos();

	// pop the current cube off the energy list
	if(cubeptr[0]->energy != cubeenergy.end()) {
		cubeenergy.erase(cubeptr[0]->energy);
	}

	// compute the energy of the volume for
	// all six possible orientations
	for (unsigned char i=0; i<6; i++) {
		e = compute_energy(cubeptr, i/2, (i%2>0));
		if(i == 0) emin = e;

		if(i/2 == cubeptr[0]->orientget() && (i%2 > 0) == cubeptr[0]->isorientneg()) {
			enow = e;
		} else if (e < emin) {
			ce.orientset(i/2);
			if(i%2 > 0)
				ce.orientneg();
			else
				ce.orientpos();
			emin = e;
		}
	}

	// if the cube can be improved, put in the list.
	if (enow > emin) {
		ce.cube = loc;
		ce.reduce = enow-emin;
		cubeptr[0]->energy = cubeenergy.insert(ce);
	} else {
		cubeptr[0]->energy = cubeenergy.end();
	}
}

double compute_energy(SpCube *cubeptr[7], unsigned char d, bool neg)
// compute the energy for a cube cubeptr[0] for a given orientation
// based on its properties and the orientation of its neighbors, cubeptr[1-6]
{
	double e = 0;
	unsigned char i;

	for (i=1;i<7;i++) {
		if (cubeptr[i]) {
			if (cubeptr[i]->orientget() != d || cubeptr[i]->isorientneg() != neg) {
				e += (1 + (cubeptr[i]->isthin() ? args.OP_THN : 0)) *
					 (1 - args.OP_NCT/6.0*cubeptr[i]->neighborcount()) *
					 (1 + (cubeptr[i]->orientget() == d == i/2 ? args.OP_BKO :
					 (cubeptr[i]->orientget() == d ? args.OP_SOR : (d == i/2 ? args.OP_BAK : 0))));
			}
		}
	}

	e *= args.OP_NBR;

	// compare orientation to cube's average normal
	// 1.0 for worst match, 0 for best
	e += args.OP_ORN * (1.0 - fabs(cubeptr[0]->avgnormal[d]));
	// compare direction to cube's average normal
	// 1 for mismatch, 0 for match
	e += args.OP_DIR * (cubeptr[0]->isthin(d) ? 0 : 1)*(neg == (cubeptr[0]->avgnormal[d] > 0));

	return e;
}

bool ldraw_grid(ofstream &ldr, char *name)
{
  if(!ldr.good())
    return false;
  else {
    ldr << "0 FILE " << name << endl;
    ldr << "0 LSculpt grid" << endl;
    ldr << "0 Author: LSculpt" << endl;
    ldr << "0 BFC CERTIFY" << endl;
    for(map<SpCubeKey,SpCube>::iterator c = cubelist.begin(); c != cubelist.end(); c++) {
      ldr << "1 16 "
        << (*c).first[0]*SPCUBE_WIDTH+SPCUBE_WIDTH/2    << " "
        << -((*c).first[1]*SPCUBE_WIDTH+SPCUBE_WIDTH/2) << " "
        << -((*c).first[2]*SPCUBE_WIDTH+SPCUBE_WIDTH/2) << " "
        << SPCUBE_WIDTH/2 << " 0 0 0 " << SPCUBE_WIDTH/2 << " 0 0 0 " << SPCUBE_WIDTH/2 << " "
        << GRID_PN << endl;
    }
    ldr << "0" << endl;
    ldr << "0 FILE " << GRID_PN << endl;
    ldr << "0 BFC CERTIFY CCW" << endl;
    ldr << "0 BFC INVERTNEXT" << endl;
    ldr << "1 16 0 0 0 1 0 0 0 1 0 0 0 1 box.dat" << endl;
    ldr << "0" << endl;

    return true;
  }
}

bool ldraw_mesh(ofstream &ldr, char *name)
{
  if(!ldr.good())
    return false;
  else {
    ldr << "0 FILE " << name << endl;
    ldr << "0 LSculpt mesh" << endl;
    ldr << "0 Author: LSculpt" << endl;
    ldr << fixed << setprecision (LDR_PREC);

    SmVector3 vtx;

    // convert each triangle to LDraw format
    for(vector<Triangle>::iterator i = inputmesh.begin(); i != inputmesh.end(); i++) {
      ldr << "3 16";

      for(char j = 0; j < 3; j++) {
        vtx = ((*i).v[j] + args.OPTS_OFFSET) / args.OPTS_SCALE;
        ldr << " " << vtx[0] << " " << -vtx[1] << " " << -vtx[2];
      }
      ldr << endl;
    }
    ldr << "0" << endl;

    return true;
  }
}

bool ldraw_plates(ofstream &ldr, char *name)
{
  if(!ldr.good())
    return false;

  unsigned char i, w, h;
  unsigned char d, e, f;
  unsigned char color;
  char o[20] = "";
  int llc[3], loc[3];
  w = SPCUBE_WIDTH/VOXEL_WIDTH;
  h = SPCUBE_WIDTH/VOXEL_HEIGHT;

  ldr << "0 FILE " << name << endl;
  ldr << "0 Author: LSculpt" << endl;
  ldr << "0 LSculpt options:" << endl;
  ldr << "0 Up vector: " << ((args.OPTS_UP == UP_Y) ? "Y" : "Z") << endl;
  ldr << "0 Rotation:  " << args.OPTS_ROT << endl;
  ldr << "0 Offset:    " << args.OPTS_OFFSET[0] << ", " << args.OPTS_OFFSET[1] << ", " << args.OPTS_OFFSET[2] << endl;
  ldr << "0 Scaling:   " << 1.0/args.OPTS_SCALE << endl << "0" << endl;
  for (map<SpCubeKey,SpCube>::iterator c = cubelist.begin(); c != cubelist.end(); c++) {

    d = (*c).second.orientget();
    e = (d+1) % 3;
    f = (d+2) % 3;

    llc[0] = (*c).first[0]*SPCUBE_WIDTH;
    llc[1] = (*c).first[1]*SPCUBE_WIDTH;
    llc[2] = (*c).first[2]*SPCUBE_WIDTH;

    llc[e] += VOXEL_WIDTH/2;
    llc[f] += VOXEL_WIDTH/2;
    llc[d] -= ((*c).second.isorientneg()) ? VOXEL_HEIGHT : 0;

    for (i=0; i < w*w*h; i++) {
      if (((*c).second.voxels[(*c).second.orientget()] >> i) & 1) {
        loc[e] = llc[e] + (i / (h*w))*VOXEL_WIDTH;
        loc[f] = llc[f] + (i % (h*w))/h*VOXEL_WIDTH;
        loc[d] = llc[d] + ((i % h)+1)*VOXEL_HEIGHT;
        switch((*c).second.orientget()) {
          case 0:
    strcpy(o, (*c).second.isorientneg() ?
              "0 1 0 -1 0 0 0 0 1" :
              "0 -1 0 1 0 0 0 0 1");
            color = (*c).second.isorientneg() ? args.OPTS_COLOR_1 : args.OPTS_COLOR_0;
            break;
          case 1:
    strcpy(o, (*c).second.isorientneg() ?
              "-1 0 0 0 -1 0 0 0 1" :
              "1 0 0 0 1 0 0 0 1");
            color = (*c).second.isorientneg() ? args.OPTS_COLOR_3 : args.OPTS_COLOR_2;
            break;
          case 2:
    strcpy(o, (*c).second.isorientneg() ?
              "1 0 0 0 0 1 0 -1 0" :
              "1 0 0 0 0 -1 0 1 0");
            color = (*c).second.isorientneg() ? args.OPTS_COLOR_5 : args.OPTS_COLOR_4;
            break;
        }

        switch (args.OPTS_COLOR) {
          case COLOR_OFF:
          default:
            color = args.OPTS_COLOR_NONE;
            break;
          case COLOR_LAY:
            switch (i % h) {
              case 0: color = args.OPTS_COLOR_0; break;
              case 1: color = args.OPTS_COLOR_1; break;
              case 2: color = args.OPTS_COLOR_2; break;
              case 3: color = args.OPTS_COLOR_3; break;
              default:
              case 4: color = args.OPTS_COLOR_4; break;
            }
            break;
          case COLOR_DIR:
            break;
        }
        ldr << "1 " << (int)color << " "
          << loc[0] << " " << -loc[1] << " " << -loc[2] << " "
          << o << " ";
        switch (args.OPTS_PART){
          case 1: ldr << VOXEL_PN_1; break;
          case 2: ldr << VOXEL_PN_2; break;
          case 3: ldr << VOXEL_PN_3; break;
          case 0: default:
            ldr << VOXEL_PN_0; break;
        }
        ldr << endl;
      }
    }
  }
  ldr << "0" << endl;
  switch (args.OPTS_PART) {
    default: break;
    case 2:
      ldr << "0 FILE " << VOXEL_PN_2 << endl;
      ldr << "1 16 0 4 0 10 0 0 0 -4 0 0 0 10 box.dat" << endl;
      ldr << "1 16 0 -2 0 3 0 -3 0 2 0 3 0 3 box.dat" << endl << "0" << endl;
      break;
    case 3:
      ldr << "0 FILE " << VOXEL_PN_3 << endl;
      ldr << "1 16 0 4 0 10 0 0 0 -4 0 0 0 10 box.dat" << endl << "0" << endl;
      break;
  }

  return true;
}


bool save_ldraw(char *fname)
{
	ofstream ldr(fname, ios::out);

	if(!ldr.good())
		return false;
  else {
    QFileInfo finfo( fname );
    QString name = finfo.baseName();
    bool multiple_parts = ((char) args.OPTS_MESH + (char) args.OPTS_PLATES + (char) args.OPTS_GRID > 1);

    if(multiple_parts) {
      ldr << "0 FILE " << qPrintable(name) << ".ldr" << endl;
      ldr << "0 BFC CERTIFY" << endl;
      if (args.OPTS_PLATES)
        ldr << "1 " << args.OPTS_COLOR_PLATES << " 0 0 0 1 0 0 0 1 0 0 0 1 " << qPrintable(name) << "_plates.ldr" << endl;
      if (args.OPTS_GRID)
        ldr << "1 " << args.OPTS_COLOR_GRID << " 0 0 0 1 0 0 0 1 0 0 0 1 " << qPrintable(name) << "_grid.ldr" << endl;
      if (args.OPTS_MESH)
        ldr << "1 " << args.OPTS_COLOR_MESH << " 0 0 0 1 0 0 0 1 0 0 0 1 " << qPrintable(name) << "_mesh.ldr" << endl;
      ldr << "0" << endl;
    }

    if (args.OPTS_PLATES) {
      ldraw_plates(ldr, QString(name).append(multiple_parts ? "_plates.ldr" : ".ldr").toUtf8().data());
    }

    if (args.OPTS_GRID) {
      ldraw_grid(ldr, QString(name).append(multiple_parts ? "_grid.ldr" : ".ldr").toUtf8().data());
    }

    if (args.OPTS_MESH) {
      ldraw_mesh(ldr, QString(name).append(multiple_parts ? "_mesh.ldr" : ".ldr").toUtf8().data());
    }

    return true;
  }
}
