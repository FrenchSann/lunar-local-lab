package lab.lunar;

import java.lang.reflect.*;
import java.net.*;
import java.nio.file.*;
import java.io.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicBoolean;

/** Explicit adapter for the named 1.8.9 runtime verified by live metadata. */
public final class GameBridge implements Runnable {
    private static GameBridge instance;
    private static final Map<String, Method> methods = new HashMap<>();
    private static final Map<String, Field> fields = new HashMap<>();
    private static final ClassLoader LOADER = GameBridge.class.getClassLoader();
    private static native int plan(long generation, long now, boolean eligible, boolean armed, boolean physical, boolean block, int period);
    private static native int chooseArmor(int[] current, int[] candidates, double threshold);
    private static native double[] select(double[] geometry, double reach, double barrier);
    private final Object mc;
    private ScheduledExecutorService timer;
    private final AtomicBoolean queued = new AtomicBoolean();
    private volatile boolean running;
    private boolean armed, lastKey, previousEligible;
    private Object previousWorld, previousPlayer, previousNetwork;
    private long generation, clicks, replacements, ticks, selections;
    private volatile String state = "installed; disarmed";
    private int period = 100, timeout = 1500;
    private double threshold = .15, reach = 3;
    private boolean repeatEnabled = true, armorEnabled = true, selectionEnabled = true;
    // Transaction: source slot, armor slot, expected original, expected replacement, stage, deadline, next observation.
    private Object sourceSlot, armorSlot, original, replacement, transactionContainer;
    private int stage;
    private long deadline, observeAfter;

    private static Class<?> cls(String name) throws Exception { return Class.forName(name, false, LOADER); }
    private static Method method(Class<?> type, String name, Class<?>... parameters) throws Exception {
        String key = type.getName() + "#" + name + Arrays.toString(parameters);
        Method m = methods.get(key);
        if (m == null) {
            Class<?> accessible=type;
            m=accessible.getMethod(name,parameters);
            while(!Modifier.isPublic(m.getDeclaringClass().getModifiers())&&accessible.getSuperclass()!=null){
                accessible=accessible.getSuperclass();m=accessible.getMethod(name,parameters);
            }
            methods.put(key,m);
        }
        return m;
    }
    private static Object call(Object value, String name) throws Exception { return method(value.getClass(),name).invoke(value); }
    private static Object call(Object value, String name, Class<?> type, Object argument) throws Exception { return method(value.getClass(),name,type).invoke(value,argument); }
    private static Field field(Class<?> type, String name) throws Exception {
        String key=type.getName()+"#"+name; Field f=fields.get(key);
        if(f==null){f=type.getField(name);fields.put(key,f);}return f;
    }
    private static Object get(Object value,String name)throws Exception{return field(value.getClass(),name).get(value);}
    private static int integer(Object value){return ((Number)value).intValue();}
    private static double number(Object value){return ((Number)value).doubleValue();}
    private static boolean yes(Object value){return Boolean.TRUE.equals(value);}
    private static Object minecraft()throws Exception{return cls("net.minecraft.client.Minecraft").getMethod("getMinecraft").invoke(null);}

    /** Invoked from the injected control thread. All application reads/writes run in its scheduled Callable. */
    public static String command(String operation) throws Exception {
        final Object game=minecraft();
        final Method schedule=game.getClass().getMethod("addScheduledTask",Callable.class);
        Callable<String> task=()->{
            if(!yes(game.getClass().getMethod("isCallingFromMinecraftThread").invoke(game)))throw new IllegalStateException("wrong application thread");
            if(instance==null)instance=new GameBridge(game);
            if(operation.equals("install")){
                if(!instance.running&&instance.stage==0)instance=new GameBridge(game);
                instance.start();
            }
            else if(operation.equals("stop"))instance.stop();
            else if(operation.equals("arm")){
                instance.observeContext();
                if(!instance.running||!instance.previousEligible)throw new IllegalStateException("ineligible context");
                if(instance.stage!=0)throw new IllegalStateException("unresolved equipment transaction");
                instance.armed=true;instance.state="armed";
            }else if(!operation.equals("status"))throw new IllegalArgumentException("unknown operation");
            return instance.status();
        };
        Future<?> future=(Future<?>)schedule.invoke(game,task);
        try{return (String)future.get(3,TimeUnit.SECONDS);}catch(TimeoutException error){future.cancel(false);throw error;}
    }
    private GameBridge(Object game)throws Exception{
        mc=game;
        // These names and types are checked against actual runtime classes before activation.
        for(String name:new String[]{"theWorld","thePlayer","currentScreen","inGameHasFocus","proxy","currentServerData","objectMouseOver","pointedEntity","playerController"})field(mc.getClass(),name);
        method(mc.getClass(),"clickMouse");
        method(cls("net.minecraft.client.multiplayer.PlayerControllerMP"),"windowClick",int.class,int.class,int.class,int.class,cls("net.minecraft.entity.player.EntityPlayer"));
        method(cls("net.minecraft.item.ItemStack"),"areItemStacksEqual",cls("net.minecraft.item.ItemStack"),cls("net.minecraft.item.ItemStack"));
        method(cls("net.minecraft.world.World"),"rayTraceBlocks",cls("net.minecraft.util.Vec3"),cls("net.minecraft.util.Vec3"),boolean.class,boolean.class,boolean.class);
        Path config=((File)get(mc,"mcDataDir")).toPath().resolve("lunar-local-lab.properties");
        Properties p=new Properties();
        if(Files.exists(config)){try(InputStream in=Files.newInputStream(config)){p.load(in);}}
        period=Integer.parseInt(p.getProperty("repeatMillis","100"));
        timeout=Integer.parseInt(p.getProperty("armorTimeoutMillis","1500"));
        threshold=Double.parseDouble(p.getProperty("armorThreshold","0.15"));
        reach=Double.parseDouble(p.getProperty("reach","3.0"));
        repeatEnabled=Boolean.parseBoolean(p.getProperty("repeatEnabled","true"));
        armorEnabled=Boolean.parseBoolean(p.getProperty("armorEnabled","true"));
        selectionEnabled=Boolean.parseBoolean(p.getProperty("selectionEnabled","true"));
        if(period<20||period>1000||timeout<500||timeout>10000||!Double.isFinite(threshold)||threshold<0||threshold>1||!Double.isFinite(reach)||reach<=0||reach>6)throw new IllegalArgumentException("invalid configuration");
        if(!Files.exists(config)){
            p.setProperty("repeatMillis",Integer.toString(period));p.setProperty("armorTimeoutMillis",Integer.toString(timeout));
            p.setProperty("armorThreshold",Double.toString(threshold));p.setProperty("reach",Double.toString(reach));
            p.setProperty("repeatEnabled","true");p.setProperty("armorEnabled","true");p.setProperty("selectionEnabled","true");
            try(OutputStream out=Files.newOutputStream(config,StandardOpenOption.CREATE_NEW)){p.store(out,"Lunar Local Lab - direct loopback only; F8 toggles, F9 stops");}
        }
    }
    private void start(){
        if(running)return;
        running=true;armed=false;state="running; press F8 to arm";
        timer=Executors.newSingleThreadScheduledExecutor(r->{Thread t=new Thread(r,"Lunar Local Lab scheduler");t.setDaemon(true);return t;});
        timer.scheduleWithFixedDelay(()->{
            if(!running||!queued.compareAndSet(false,true))return;
            try{mc.getClass().getMethod("addScheduledTask",Runnable.class).invoke(mc,this);}
            catch(Exception error){queued.set(false);running=false;state="scheduling failed";timer.shutdown();}
        },0,20,TimeUnit.MILLISECONDS);
    }
    private void stop(){running=false;armed=false;state=stage==0?"stopped":"stopped with unresolved cursor transaction; inspect inventory";if(timer!=null)timer.shutdown();}
    private Object network()throws Exception{Object handler=call(mc,"getNetHandler");return handler==null?null:call(handler,"getNetworkManager");}
    private boolean local(Object net)throws Exception{
        if(net==null||get(mc,"theWorld")==null||get(mc,"thePlayer")==null||get(mc,"currentScreen")!=null||!yes(get(mc,"inGameHasFocus")))return false;
        return localPeer(net);
    }
    private boolean localPeer(Object net)throws Exception{
        if(net==null)return false;
        if(((java.net.Proxy)get(mc,"proxy")).type()!=java.net.Proxy.Type.DIRECT)return false;
        Object address=call(net,"getRemoteAddress");
        if(!(address instanceof InetSocketAddress))return false;
        InetSocketAddress peer=(InetSocketAddress)address;
        if(peer.isUnresolved()||!peer.getAddress().isLoopbackAddress())return false;
        Object server=get(mc,"currentServerData");if(server==null)return false;
        String host=(String)get(server,"serverIP");
        return host.equals("127.0.0.1")||host.equals("::1")||host.equals("[::1]")||host.matches("127\\.0\\.0\\.1:[0-9]{1,5}")||host.matches("\\[::1\\]:[0-9]{1,5}");
    }
    private void observeContext()throws Exception{
        Object world=get(mc,"theWorld"),player=get(mc,"thePlayer"),net=network();boolean eligible=local(net);
        if(world!=previousWorld||player!=previousPlayer||net!=previousNetwork||eligible!=previousEligible){
            if(world!=previousWorld||player!=previousPlayer||net!=previousNetwork){stage=0;sourceSlot=armorSlot=original=replacement=transactionContainer=null;}
            ++generation;armed=false;if(stage!=0)state="context changed with unresolved equipment transaction";else state="context changed; disarmed";
            previousWorld=world;previousPlayer=player;previousNetwork=net;previousEligible=eligible;
        }
    }
    private String status()throws Exception{
        observeContext();return "state="+state+"; running="+running+"; armed="+armed+"; world="+(get(mc,"theWorld")!=null)+"; loopbackPeer="+localPeer(network())+"; focused="+get(mc,"inGameHasFocus")+"; menu="+(get(mc,"currentScreen")!=null)+"; eligible="+previousEligible+"; ticks="+ticks+"; clicks="+clicks+"; armorConfirmedLocally="+replacements+"; equipmentStage="+stage+"; selections="+selections+"; reach="+reach;
    }
    public void run(){
        try{
            if(!running)return;
            if(!yes(call(mc,"isCallingFromMinecraftThread")))throw new IllegalStateException("wrong thread");
            ++ticks;observeContext();
            Class<?> keyboard=cls("org.lwjgl.input.Keyboard");
            int f8=keyboard.getField("KEY_F8").getInt(null),f9=keyboard.getField("KEY_F9").getInt(null);
            Method key=method(keyboard,"isKeyDown",int.class);
            boolean pressed=yes(key.invoke(null,f8));
            if(pressed&&!lastKey&&previousEligible&&stage==0){armed=!armed;state=armed?"armed":"disarmed";}
            lastKey=pressed;
            if(yes(key.invoke(null,f9))){stop();return;}
            boolean physical=yes(method(cls("org.lwjgl.input.Mouse"),"isButtonDown",int.class).invoke(null,0));
            Object hit=get(mc,"objectMouseOver");
            boolean block=hit!=null&&((Enum<?>)get(hit,"typeOfHit")).name().equals("BLOCK");
            long now=System.nanoTime()/1000000L;
            if(!armed||!previousEligible){
                plan(generation,now,false,false,false,false,period);
                if(previousEligible&&stage!=0&&now>=deadline){
                    Object player=get(mc,"thePlayer");
                    if(get(player,"openContainer")==transactionContainer&&cursor(player)==null&&
                       ((same(stack(sourceSlot),original)&&same(stack(armorSlot),replacement))||
                        (same(stack(sourceSlot),replacement)&&same(stack(armorSlot),original)))){
                        stage=0;sourceSlot=armorSlot=original=replacement=transactionContainer=null;state="inventory reobserved; disarmed";
                    }
                }
                return;
            }
            // Vanilla retains ownership of a held attack on a block. Never inject repeated block clicks.
            if(physical&&block){plan(generation,now,true,true,true,true,period);return;}
            if(stage!=0){plan(generation,now,true,false,false,false,period);advanceEquipment(now);return;}
            if(armorEnabled&&beginEquipment(now)){plan(generation,now,true,false,false,false,period);return;}
            int action=plan(generation,now,true,true,repeatEnabled&&physical,false,period);
            if((action&1)!=0){
                Object oldHit=get(mc,"objectMouseOver"),oldPointed=get(mc,"pointedEntity");
                try{
                    if(selectionEnabled)selectTarget();
                    if(!local(network())){armed=false;return;}
                    call(mc,"clickMouse");++clicks;
                }finally{field(mc.getClass(),"objectMouseOver").set(mc,oldHit);field(mc.getClass(),"pointedEntity").set(mc,oldPointed);}
            }
        }catch(Exception|LinkageError error){armed=false;state="error: "+error.getClass().getSimpleName();if(stage!=0)state+="; inspect inventory cursor";}
        finally{queued.set(false);}
    }
    private Object stack(Object slot)throws Exception{return call(slot,"getStack");}
    private Object cursor(Object player)throws Exception{return call(get(player,"inventory"),"getItemStack");}
    private boolean same(Object a,Object b)throws Exception{return yes(method(cls("net.minecraft.item.ItemStack"),"areItemStacksEqual",cls("net.minecraft.item.ItemStack"),cls("net.minecraft.item.ItemStack")).invoke(null,a,b));}
    private int[] armor(Object itemStack,int id)throws Exception{
        if(itemStack==null)return null;Object item=call(itemStack,"getItem");if(!cls("net.minecraft.item.ItemArmor").isInstance(item))return null;
        int max=integer(call(itemStack,"getMaxDamage")),remaining=max-integer(call(itemStack,"getItemDamage"));
        int attributes=0;
        Object tags=call(itemStack,"getEnchantmentTagList");
        if(tags!=null){int count=integer(call(tags,"tagCount"));for(int i=0;i<count;++i){Object tag=call(tags,"getCompoundTagAt",int.class,i);int level=integer(call(tag,"getShort",String.class,"lvl"));attributes+=Math.max(0,level)*10;}}
        Object modifiers=call(itemStack,"getAttributeModifiers");
        Collection<?> entries=(Collection<?>)method(cls("com.google.common.collect.Multimap"),"entries").invoke(modifiers);
        for(Object entry:entries){Object modifier=((Map.Entry<?,?>)entry).getValue();double amount=number(call(modifier,"getAmount"));if(Double.isFinite(amount))attributes+=Math.min(10000,(int)Math.max(0,amount*100));}
        return new int[]{id,integer(get(item,"armorType")),integer(get(item,"damageReduceAmount")),Math.min(attributes,1000000),remaining,max};
    }
    private void clickSlot(Object slot)throws Exception{
        Object player=get(mc,"thePlayer"),container=get(player,"openContainer");
        if(container!=transactionContainer||!local(network()))throw new IllegalStateException("equipment context changed");
        method(get(mc,"playerController").getClass(),"windowClick",int.class,int.class,int.class,int.class,cls("net.minecraft.entity.player.EntityPlayer"))
            .invoke(get(mc,"playerController"),integer(get(container,"windowId")),integer(get(slot,"slotNumber")),0,0,player);
    }
    private boolean beginEquipment(long now)throws Exception{
        Object player=get(mc,"thePlayer"),inventory=get(player,"inventory"),container=get(player,"inventoryContainer");
        if(get(player,"openContainer")!=container||cursor(player)!=null)return false;
        List<?> slots=(List<?>)get(container,"inventorySlots");Object[] worn=(Object[])get(inventory,"armorInventory");
        for(Object current:worn){
            int[] currentInfo=armor(current,1);if(currentInfo==null||currentInfo[5]<=0||(double)currentInfo[4]/currentInfo[5]>=threshold)continue;
            Object equippedSlot=null;List<Object> candidates=new ArrayList<>();List<Integer> values=new ArrayList<>();
            for(Object slot:slots){
                if(get(slot,"inventory")!=inventory)continue;
                Object value=stack(slot);
                if(value==current){equippedSlot=slot;continue;}
                // Actual inventory index determines that this is a main-inventory slot, not equipped armor.
                int index=integer(get(slot,"slotIndex"));Object[] main=(Object[])get(inventory,"mainInventory");
                if(index<0||index>=main.length||value!=main[index])continue;
                int[] data=armor(value,candidates.size()+2);if(data==null)continue;
                candidates.add(slot);for(int n:data)values.add(n);
            }
            if(equippedSlot==null)continue;
            int[] packed=new int[values.size()];for(int i=0;i<packed.length;++i)packed[i]=values.get(i);
            int id=chooseArmor(currentInfo,packed,threshold);if(id<2)continue;
            Object source=candidates.get(id-2),candidate=stack(source);
            if(!yes(call(equippedSlot,"isItemValid",cls("net.minecraft.item.ItemStack"),candidate)))continue;
            sourceSlot=source;armorSlot=equippedSlot;original=call(current,"copy");replacement=call(candidate,"copy");transactionContainer=container;
            stage=1;deadline=now+timeout;observeAfter=now+100;
            clickSlot(sourceSlot);state="equipment pickup; awaiting local observation";return true;
        }
        return false;
    }
    private void advanceEquipment(long now)throws Exception{
        if(now>=deadline){armed=false;state="equipment timeout; reobserve inventory before restart";return;}
        if(now<observeAfter)return;
        Object player=get(mc,"thePlayer");
        if(get(player,"openContainer")!=transactionContainer){armed=false;state="container changed; inspect cursor";return;}
        Object held=cursor(player);
        if(stage==1&&same(held,replacement)&&stack(sourceSlot)==null&&same(stack(armorSlot),original)){
            clickSlot(armorSlot);stage=2;observeAfter=now+100;
        }else if(stage==2&&same(held,original)&&same(stack(armorSlot),replacement)&&stack(sourceSlot)==null){
            clickSlot(sourceSlot);stage=3;observeAfter=now+200;
        }else if(stage==3&&held==null&&same(stack(sourceSlot),original)&&same(stack(armorSlot),replacement)){
            stage=0;sourceSlot=armorSlot=original=replacement=transactionContainer=null;++replacements;state="armor locally confirmed; server authoritative";
        }
    }
    private double[] vector(Object value)throws Exception{return new double[]{number(get(value,"xCoord")),number(get(value,"yCoord")),number(get(value,"zCoord"))};}
    private Object vec(double x,double y,double z)throws Exception{return cls("net.minecraft.util.Vec3").getConstructor(double.class,double.class,double.class).newInstance(x,y,z);}
    private void selectTarget()throws Exception{
        Object player=get(mc,"thePlayer"),world=get(mc,"theWorld"),view=call(mc,"getRenderViewEntity");if(view!=player)return;
        Object eyes=call(player,"getPositionEyes",float.class,1f);double[] eye=vector(eyes),direction=vector(call(player,"getLook",float.class,1f));
        Object end=vec(eye[0]+direction[0]*reach,eye[1]+direction[1]*reach,eye[2]+direction[2]*reach);
        Object obstacle=method(world.getClass(),"rayTraceBlocks",cls("net.minecraft.util.Vec3"),cls("net.minecraft.util.Vec3"),boolean.class,boolean.class,boolean.class).invoke(world,eyes,end,false,true,false);
        double barrier=Double.POSITIVE_INFINITY;
        if(obstacle!=null){double[] hit=vector(get(obstacle,"hitVec"));barrier=Math.sqrt(Math.pow(hit[0]-eye[0],2)+Math.pow(hit[1]-eye[1],2)+Math.pow(hit[2]-eye[2],2));}
        List<Object> targets=new ArrayList<>();List<Double> coords=new ArrayList<>();for(double d:eye)coords.add(d);for(double d:direction)coords.add(d);
        for(Object entity:(List<?>)get(world,"loadedEntityList")){
            if(entity==player||!yes(call(entity,"isEntityAlive"))||!yes(call(entity,"canBeCollidedWith")))continue;
            Object box=call(entity,"getEntityBoundingBox");double border=number(call(entity,"getCollisionBorderSize"));
            for(String field:new String[]{"minX","minY","minZ"})coords.add(number(get(box,field))-border);
            for(String field:new String[]{"maxX","maxY","maxZ"})coords.add(number(get(box,field))+border);
            targets.add(entity);if(targets.size()>=4096)break;
        }
        double[] data=new double[coords.size()];for(int i=0;i<data.length;++i)data[i]=coords.get(i);
        double[] selected=select(data,reach,barrier);if(selected==null)return;
        Object target=targets.get((int)selected[0]);double distance=selected[1];
        Object position=vec(eye[0]+direction[0]*distance,eye[1]+direction[1]*distance,eye[2]+direction[2]*distance);
        Object hit=cls("net.minecraft.util.MovingObjectPosition").getConstructor(cls("net.minecraft.entity.Entity"),cls("net.minecraft.util.Vec3")).newInstance(target,position);
        field(mc.getClass(),"objectMouseOver").set(mc,hit);field(mc.getClass(),"pointedEntity").set(mc,target);++selections;
    }
}
