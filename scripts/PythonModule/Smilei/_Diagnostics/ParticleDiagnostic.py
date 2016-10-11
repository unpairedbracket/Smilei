from .Diagnostic import Diagnostic
from .._Utils import *

# -------------------------------------------------------------------
# Class for particle diagnostics
# -------------------------------------------------------------------
class ParticleDiagnostic(Diagnostic):
	# This is the constructor, which creates the object
	def _init(self, diagNumber=None, timesteps=None, slice=None, data_log=False, stride=1, **kwargs):
		
		if diagNumber is None:
			self._error += "Printing available particle diagnostics:\n"
			self._error += "----------------------------------------\n"
			diags = self.getDiags()
			for diagNumber in diags:
				self._error += self._printInfo(self._getInfo(diagNumber))
			if len(diags)==0:
				self._error += "      No particle diagnostics found in "+self._results_path
			return
		
		cell_size = {"x":self._cell_length[0]}
		if self._ndim>1: cell_size.update({"y":self._cell_length[1]})
		if self._ndim>2: cell_size.update({"z":self._cell_length[2]})
		
		# 1 - verifications, initialization
		# -------------------------------------------------------------------
		# Check the requested diags are ok
		if type(diagNumber) is int:
			if diagNumber<0:
				self._error = "Argument 'diagNumber' cannot be a negative integer."
				return
			self.operation = '#' + str(diagNumber)
		elif type(diagNumber) is str:
			self.operation = diagNumber
		else:
			self._error = "Argument 'diagNumber' must be and integer or a string."
			return
		
		# Get list of requested diags
		self._diags = sorted(set([ int(d[1:]) for d in self._re.findall('#\d+',self.operation) ]))
		for diag in self._diags:
			if not self._getInfo(diag):
				self._error = "No particle diagnostic #"+str(diag)
				return
		try:
			exec(self._re.sub('#\d+','1.',self.operation))
		except ZeroDivisionError: pass
		except:
			self._error = "Cannot understand operation '"+self.operation+"'"
			return
		# Verify that all requested diags exist and they all have the same shape
		self._myinfo = {}
		self._axes = {}
		self._naxes = {}
		for d in self._diags:
			try:
				self._myinfo.update({ d:self._getInfo(d) })
			except:
				self._error = "Particle diagnostic #"+str(d)+" not found."
				return
			self._axes .update ({ d:self._myinfo[d]["axes"] })
			self._naxes.update ({ d:len(self._axes[d]) })
			if self._naxes[d] != self._naxes[self._diags[0]]:
				self._error = "All diagnostics in operation '"+self.operation+"' must have as many axes." \
					+ " Diagnotic #"+str(d)+" has "+str(self._naxes[d])+" axes and #"+ \
					str(self._diags[0])+" has "+str(self._naxes[self._diags[0]])+" axes"
				return
			for a in self._axes[d]:
				if self._axes[d] != self._axes[self._diags[0]]:
					self._error = "In operation '"+self.operation+"', diagnostics #"+str(d)+" and #"\
						+str(self._diags[0])+" must have the same shape."
					return
		
		self._axes  = self._axes [self._diags[0]]
		self._naxes = self._naxes[self._diags[0]]
		
		# Check slice is a dict
		if slice is not None  and  type(slice) is not dict:
			self._error = "Argument 'slice' must be a dictionary"
			return
		# Make slice a dictionary
		if slice is None: slice = {}
		
		# Put data_log as object's variable
		self._data_log = data_log
		
		# 2 - Manage timesteps
		# -------------------------------------------------------------------
		# Get available timesteps
		self._file = {}
		self.times = {}
		self._indexOfTime  = {}
		self._h5items = {}
		for d in self._diags:
			# get all diagnostics files
			self._file.update({ d:self._h5py.File(self._results_path+'/ParticleDiagnostic'+str(d)+'.h5') })
			self._h5items.update({ d:list(self._file[d].values()) })
			# get all diagnostics timesteps
			self.times.update({ d:self.getAvailableTimesteps(d) })
			# fill the "_indexOfTime" dictionary with indices to the data arrays
			self._indexOfTime.update({ d:{} })
			for i,t in enumerate(self.times[d]):
				self._indexOfTime[d].update({ t : i })
			# If timesteps is None, then keep all timesteps, otherwise, select timesteps
			if timesteps is not None:
				try:
					self.times[d] = self._selectTimesteps(timesteps, self.times[d])
				except:
					self._error = "Argument 'timesteps' must be one or two non-negative integers"
					return
			# Verify that timesteps are the same for all diagnostics
			if (self.times[d] != self.times[self._diags[0]]).any() :
				self._error = "All diagnostics in operation '"+self.operation+"' must have the same timesteps."\
					+" Diagnotic #"+str(d)+" has "+str(len(self.times[d]))+ " timesteps and #"\
					+str(self._diags[0])+" has "+str(len(self.times[self._diags[0]]))+ " timesteps"
				return
		# Now we need to keep only one array of timesteps because they should be all the same
		self.times = self.times[self._diags[0]]
		
		# Need at least one timestep
		if self.times.size < 1:
			self._error = "Timesteps not found"
			return None
		
		# 3 - Manage axes
		# -------------------------------------------------------------------
		# Fabricate all axes values for all diags
		plot_diff = []
		coeff = 1.
		unitsa = [0,0,0,0]
		spatialaxes = {"x":False, "y":False, "z":False}
		self._finalShape = []
		self._slices = []
		self._selection = ()
		
		for axis in self._axes:
			# Find the vector of values along the axis
			if axis["log"]:
				edges = self._np.linspace(self._np.log10(axis["min"]), self._np.log10(axis["max"]), axis["size"]+1)
				centers = edges + (edges[1]-edges[0])/2.
				edges = 10.**edges
				centers = 10.**(centers[:-1])
			else:
				edges = self._np.linspace(axis["min"], axis["max"], axis["size"]+1)
				centers = edges + (edges[1]-edges[0])/2.
				centers = centers[:-1]
			axis.update({ "edges"   : edges   })
			axis.update({ "centers" : centers })
			
			# Find some quantities depending on the axis type
			overall_min = "-inf"; overall_max = "inf"
			axis_units = ""
			if   axis["type"] in ["x","y","z"]:
				axis_units = "L_r"
				spatialaxes[axis["type"]] = True
				coeff *= cell_size[axis["type"]]
			elif axis["type"] in ["px","py","pz","p"]:
				axis_units = "P_r"
			elif axis["type"] in ["vx","vy","vz","v"]:
				axis_units = "V_r"
			elif axis["type"] == "gamma":
				overall_min = "1"
			elif axis["type"] == "ekin":
				axis_units = "K_r"
				overall_min = "0"
			elif axis["type"] == "charge":
				axis_units = "Q_r"
				overall_min = "0"
			
			# if this axis has to be sliced, then select the slice
			if axis["type"] in slice:
			
				self._slices.append(True)
				
				# if slice is "all", then all the axis has to be summed
				if slice[axis["type"]] == "all":
					axis.update({ "sliceInfo" : "      Slicing for all "+axis["type"] })
					self._selection += ( self._np.s_[:], )
					self._finalShape.append( axis["size"] )
					slice_size = edges[-1] - edges[0]
				
				# Otherwise, get the slice from the argument `slice`
				else:
					try:
						s = self._np.double(slice[axis["type"]])
						if s.size>2 or s.size<1: raise
					except:
						self._error = "Slice along axis "+axis["type"]+" should be one or two floats"
						return
					# convert the slice into a range of indices
					if s.size == 1:
						indices = self._np.array([(self._np.abs(centers-s)).argmin()])
					else :
						indices = self._np.nonzero( (centers>=s[0]) * (centers<=s[1]) )[0]
					if indices.size == 1:
						axis.update({ "sliceInfo" : "      Slicing at "+axis["type"]+" = "+str(centers[indices][0]) })
						self._selection += ( self._np.s_[indices[0]], )
						self._finalShape.append( 1 )
					else:
						axis.update({ "sliceInfo" : "      Slicing "+axis["type"]+" from "+str(edges[indices[0]])+" to "+str(edges[indices[-1]+1]) })
						self._selection += ( self._np.s_[indices[0]:indices[-1]], )
						self._finalShape.append( indices[-1] - indices[0] )
					# calculate the size of the slice
					slice_size = edges[indices[-1]+1] - edges[indices[0]]
				
				if axis["type"] in ["x","y","z"]: coeff /= slice_size
			
			# if not sliced, then add this axis to the overall plot
			else:
				self._selection += ( self._np.s_[::stride], )
				self._slices .append(False)
				self._type   .append(axis["type"])
				self._shape  .append(axis["size"])
				self._centers.append(centers[::stride])
				self._finalShape.append( len(self._centers[-1]) )
				self._log    .append(axis["log"])
				self._label  .append(axis["type"])
				self._units  .append(axis_units)
				plot_diff.append(self._np.diff(edges)[::stride])
		
		if len(self._shape) > 2:
			self._error = "Cannot plot in "+str(len(self._shape))+"d. You need to 'slice' some axes."
			return
		
		# Build units
		titles = {}
		units = {}
		for d in self._diags:
			titles.update({ d:"??" })
			units.update({ d:"??" })
			val_units = "??"
			output = self._myinfo[d]["output"]
			if   output == "density":
				titles[d] = "Number density"
				val_units = "N_r"
			elif output == "charge_density":
				titles[d] = "Charge density"
				val_units = "N_r * Q_r"
			elif output=="ekin_density":
				titles[d] = "Energy density"
				val_units = "N_r * K_r"
			elif output[0] == "j":
				titles[d] = "J"+output[1]
				val_units = "J_r"
			elif output[0]=="p" and output[-8:] == "_density":
				titles[d] = "P"+output[1].strip("_")+" density"
				val_units = "N_r * P_r"
			elif output[:8]=="pressure":
				titles[d] = "Pressure "+output[-2]
				val_units = "N_r * K_r"
			axes_units = [unit for unit in self._units if unit!="L_r"]
			units[d] = val_units
			if len(axes_units)>0: units[d] += " / ( " + " * ".join(axes_units) + " )"
		# Make total units and title
		self._vunits = self.operation
		self._title  = self.operation
		for d in self._diags:
			self._vunits = self._vunits.replace("#"+str(d), "( "+units[d]+" )")
			self._title  = self._title .replace("#"+str(d), titles[d])
		
		# If any spatial dimension did not appear, then count it for calculating the correct density
		if self._ndim>=1 and not spatialaxes["x"]: coeff /= self._ncels[0]
		if self._ndim>=2 and not spatialaxes["y"]: coeff /= self._ncels[1]
		if self._ndim==3 and not spatialaxes["z"]: coeff /= self._ncels[2]
		
		# Calculate the array that represents the bins sizes in order to get units right.
		# This array will be the same size as the plotted array
		if len(plot_diff)==0:
			self._bsize = 1.
		elif len(plot_diff)==1:
			self._bsize = plot_diff[0]
		else:
			self._bsize = self._np.prod( self._np.array( self._np.meshgrid( *plot_diff ) ), axis=0)
			self._bsize = self._bsize.transpose()
		self._bsize /= coeff
		self._bsize = self._np.squeeze(self._bsize)
		
		# Finish constructor
		self.valid = True
	
	# Gets info about diagnostic number "diagNumber"
	def _getInfo(self,diagNumber):
		# path to the file
		file = self._results_path+'/ParticleDiagnostic'+str(diagNumber)+'.h5'
		# if no file, return
		if not self._os.path.isfile(file): return False
		# open file
		f = self._h5py.File(file, 'r')
		# get attributes from file
		attrs = list(f.attrs.items())
		axes = []
		# Parse each attribute
		for i in range(len(attrs)):
			name  = attrs[i][0]
			value = attrs[i][1]
			if (name == "output"): output = bytes.decode(value)
			if (name == "time_average"): time_average = int(value)
			if (name == "species"):
				species = bytes.decode(value.strip()).split(" ") # get all species numbers
				for i in range(len(species)):
					species[i] = int(species[i]) # convert string to int
			if (name[0:4] == "axis" ):
				n = int(name[4:]) # axis number
				sp = bytes.decode(value).split(" ")
				axistype  = sp[0]
				axismin  = float(sp[1])
				axismax  = float(sp[2])
				axissize = int(sp[3])
				logscale = bool(int(sp[4]))
				edge_inclusive = bool(int(sp[5]))
				while len(axes)<n+1: axes.append({}) # extend the array to the adequate size
				axes[n] = {"type":axistype,"min":axismin,"max":axismax,"size":axissize,"log":logscale,"edges_included":edge_inclusive}
		f.close()
		return {"#":diagNumber, "output":output, "tavg":time_average, "species":species, "axes":axes}
	
	
	# Prints the info obtained by the function "getInfo"
	@staticmethod
	def _printInfo(info):
		# 1 - diag number, type and list of species
		species = ""
		for i in range(len(info["species"])): species += str(info["species"][i])+" " # reconstitute species string
		printedInfo = "Diag#"+str(info["#"])+" - "+info["output"]+" of species # "+species+"\n"
		
		# 2 - period and time-averaging
		tavg = "no time-averaging"
		if (info["tavg"] > 1):
			printedInfo += "    Averaging over "+str(info["tavg"])+" timesteps\n"
		
		# 3 - axes
		for i in range(len(info["axes"])):
			axis = info["axes"][i];
			logscale = "" if not axis["log"] else " [ LOG SCALE ] "
			edges    = "" if not axis["edges_included"] else " [ INCLUDING EDGES ] "
			printedInfo += "    "+axis["type"]+" from "+str(axis["min"])+" to "+str(axis["max"]) \
				   +" in "+str(axis["size"])+" steps "+logscale+edges+"\n"
		return printedInfo
		
	# Method to print info on all included diags
	def _info(self):
		info = ""
		for d in self._diags:
			info += self._printInfo(self._myinfo[d])+"\n"
		if len(self.operation)>2: info += "Operation : "+self.operation+"\n"
		for ax in self._axes:
			if "sliceInfo" in ax: info += ax["sliceInfo"]+"\n"
		return info
	
	def getDiags(self):
		files = self._glob(self._results_path+"/ParticleDiagnostic*.h5")
		diags = [int(self._re.findall(r"ParticleDiagnostic([0-9]+)[.]h5$",file)[0]) for file in files]
		return diags
	
	# get all available timesteps for a given diagnostic
	def getAvailableTimesteps(self, diagNumber=None):
		# if argument "diagNumber" not provided, return the times calculated in __init__
		if diagNumber is None:
			return self.times
		# Otherwise, get the timesteps specifically available for the single requested diagnostic
		else:
			try:
				file = self._results_path+'/ParticleDiagnostic'+str(diagNumber)+'.h5'
				f = self._h5py.File(file, 'r')
			except:
				print("Cannot open file "+file)
				return self._np.array([])
			items = list(f.items())
			ntimes = len(items)
			times = self._np.zeros(ntimes)
			for i in range(ntimes):
				times[i] = int(items[i][0].strip("timestep")) # fill the "times" array with the available timesteps
			f.close()
			return times
	
	# Method to obtain the data only
	def _getDataAtTime(self, t):
		if not self._validate(): return
		# Get arrays from all requested diagnostics
		A = {}
		for d in self._diags:
			# find the index of the array corresponding to the requested timestep
			try:
				index = self._indexOfTime[d][t]
			except: 
				print("Timestep "+str(t)+" not found in this diagnostic")
				return []
			# get data
			B = self._np.zeros(self._finalShape)
			self._h5items[d][index].read_direct(B, source_sel=self._selection) # get array
			B[self._np.isnan(B)] = 0.
			# Apply the slicing
			for iaxis in range(self._naxes):
				if self._slices[iaxis]:
					B = self._np.sum(B, axis=iaxis, keepdims=True) # sum over the slice
			# remove sliced axes
			B = self._np.squeeze(B)
			# Divide by the bins size
			B /= self._bsize
			# Append this diag's array for the operation
			A.update({ d:B })
		# Calculate operation
		data_operation = self.operation
		for d in reversed(self._diags):
			data_operation = data_operation.replace("#"+str(d),"A["+str(d)+"]")
		A = eval(data_operation)
		# log scale if requested
		if self._data_log: A = self._np.log10(A)
		return A
